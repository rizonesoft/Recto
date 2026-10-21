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
- -> XREF: D03 T03 §12 -- page delete, move and replace keep named destinations valid (D03 T03 §12).
- -> XREF: D01 T02 §8 -- certificate decryption supplies the private key through the document actor.
- -> XREF: D04 T05 §7 -- backgrounds are written as artifacts in tagged documents.
- -> XREF: D04 T06 §10 -- page operations keep PDF/X output intents (D04 T06 §10).
- -> XREF: D09 T04 §14 -- Recto Cloud's PAdES seal (D09 T04 §14) follows this signer's conventions.
- -> XREF: D08 T02 §9 -- GraftMap.
- -> XREF: D08 T02 §9 -- merge.
- -> XREF: D08 T02 §22 -- page operations.
- -> XREF: D06 T02 §8 -- FIPS gate in encryption.
- -> XREF: D06 T02 §8 -- FIPS gate in certificate encryption.
- -> XREF: D04 T02 §2 -- binder writer.
- -> XREF: D02 T03 §4 -- page labels in thumbnails.
- -> XREF: D04 T07 §5 -- redact-by-pattern packs.
- -> XREF: D05 T02 §9 -- sanitize counts from the inspector.
- -> XREF: D03 T01 §10 -- compare engine.
- -> XREF: D02 T04 §6 -- trust settings.
- -> XREF: D02 T03 §21 -- page insertion.
- -> XREF: D02 T03 §24 -- GraftMap.
- -> XREF: D04 T07 §17 -- crop step.
- -> XREF: D04 T07 §18 -- remove marks step.
- -> XREF: D04 T07 §18 -- delete blank pages step.
- -> XREF: D04 T07 §19 -- insert pdf step.
- -> XREF: D04 T08 §3 -- verb specs.
- -> XREF: D01 T03 §10 -- organize verbs follow the registration rule.

## Outcome

- `GraftMap` (`§1`) and multi-document `merge` / `split` (`§2`) combine or partition PDFs with byte-level page content preserved by object grafting (no re-render) and shared resource deduplication per source.
- Copy-based page-tree edit lists (`reorder`, `/Rotate` delta, `delete`, `extract` in `§3`), blank-page insertion and `/MediaBox`/`/CropBox`/`/TrimBox`/`/BleedBox`/`/ArtBox` cropping + page rescaling (`§4`) round-trip through render comparison.
- Vector and image `Watermarks & Stamps` (`§5`) and 3-zone `Headers, Footers & Legal Bates Numbering` (`§6`) inject reusable Form XObjects with shrink-to-avoid-overlap protection.
- Encryption (`§7`) produces `AES-256` (`V=5, R=6`) files with the stated permissions that other readers honor; decryption with the right password produces a clean unprotected copy, and the wrong password is a typed refusal.
- `SignatureEngine` (`src/pdf/signature.cpp`, `§8`, `§9`) signs and verifies PDFs using PKCS#12 (`.pfx`/`.p12`) and the Windows Certificate Store (`crypt32.dll` / `bcrypt.dll`) with PKCS#7 detached and PAdES (`B-B`, `B-T`, `B-LT`), `/DSS` revocation data, RFC 3161 timestamps, `/DocMDP` certification, and incremental-save multi-signature support.
- `Redactor` (`src/pdf/redactor.cpp`, `§10`) permanently removes text runs, vector paths, and image pixels inside redaction rectangles and sanitizes hidden metadata, scripts, attachments, and layers.
- Certificate (public-key) encryption (`§11`, `§12`) encrypts a PDF for the X.509 certificates of named recipients with the `/Adobe.PubSec` security handler and `AES-256`, with per-recipient permissions, and opens such a file with the user's certificate private key through CNG, never falling back to an unencrypted or weaker file.
- Page backgrounds (`§13`) add colour, image or PDF-page backgrounds as managed marks that can be listed, updated and removed later, and list and remove watermarks, without rewriting the original page content streams.
- Replace pages (`§14`) replaces any set of pages with pages from another PDF on a copy, remapping or removing bookmarks, links, named destinations, form fields and tags of the replaced pages so no reference dangles.
- Page tools beyond Acrobat parity (`§15` to `§23`): split by file size and by top-level bookmarks, split one page into several, N-up sheets, interleave, swap odd and even and reverse, page label editing that survives every page operation, blank page detection, reorder by bookmarks, a paging seal and ID card copy, all copy operations with a CLI verb.
- Redaction and inspection beyond Acrobat parity (`§24` to `§27`): a labelled whiteout and whole-page redaction, country PII packs with checksums, versioned redaction profiles and a document inspector report, all local and never executing content.
- Signature and binder tools (`§28` to `§30`): the signed version of a document and its comparison with the current one, a Recto-only trust store with opt-in download and manual import, and a PDF Binder with a linked table of contents and a rebuildable project.

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
| 6 | §6 | 3-zone `Headers & Footers` and legal **Bates Numbering** (`stamp_watermark.cpp` & CLI) | §5, §13 | [ ] |
| 7 | §7 | Windows CNG (`bcrypt.dll`) `AES-256` (`V=5, R=6`) encrypt/decrypt writer and CLI verbs | §1, D01 T01 §9 | [ ] |
| 8 | §8 | X.509 / PKCS#12 and Windows Certificate Store `PKCS#7` & `PAdES` signer (`cli sign`) | §7, D01 T02 §5 | [ ] |
| 9 | §9 | Cryptographic signature verifier (`/ByteRange`, CMS/CAdES, chain trust & tamper check) | §8 | [ ] |
| 10 | §10 | True physical content-stream redactor (glyph/path/image scrubbing) & `sanitize_document` | §3, D03 T01 §1 | [ ] |
| 11 | §11 | `/Adobe.PubSec` certificate encryption writer: recipients, CMS envelopes, AES-256 | §7, D04 T02 §1, D04 T02 §2 | [ ] |
| 12 | §12 | `/Adobe.PubSec` decryption with a certificate private key through CNG on open | §11, D01 T01 §9, D01 T02 §8 | [ ] |
| 13 | §13 | Page backgrounds engine: add, update, list and remove managed marks | §1, §3, §5 | [ ] |
| 14 | §14 | `replace_pages`: replace pages from another PDF, remapping links, fields and tags | §1, §2, §3 | [ ] |
| 15 | §15 | Split by maximum file size and by top-level bookmarks (engine and CLI) | §2, D01 T01 §10 | [ ] |
| 16 | §16 | Split one page into several: grid or custom cut lines (engine and CLI) | §3, §10, §14, §19 | [ ] |
| 17 | §17 | N-up sheets: several pages on one sheet as a new PDF (engine and CLI) | §1, §3 | [ ] |
| 18 | §18 | Interleave two PDFs, swap odd and even pages, reverse page order (engine and CLI) | §2, §3, §19 | [ ] |
| 19 | §19 | Page labels: read, edit, preview and carry through page operations (engine and CLI) | §2, §3, §14 | [ ] |
| 20 | §20 | Blank page detection with a local heuristic and a deletion plan (engine and CLI) | §3, D01 T02 §1, D01 T02 §6 | [ ] |
| 21 | §21 | Reorder pages by bookmark order (engine and CLI) | §3, §19, D01 T01 §10 | [ ] |
| 22 | §22 | Cross-page paging seal: split a seal image across page edges (engine and CLI) | §5, §13 | [ ] |
| 23 | §23 | ID card copy: front and back images on one page (engine and CLI) | D04 T02 §2, D04 T02 §5 | [ ] |
| 24 | §24 | Whiteout and whole-page redaction (engine and CLI) | §10 | [ ] |
| 25 | §25 | PII country packs with checksum validation, local only (engine and CLI) | D03 T01 §1 | [ ] |
| 26 | §26 | Redaction profiles: saved patterns, appearance and scope as a JSON file (engine and CLI) | §10, §25, D03 T01 §5 | [ ] |
| 27 | §27 | Document inspector report: what a PDF carries before sanitizing (engine and CLI) | §10, D01 T01 §10, D01 T02 §1 | [ ] |
| 28 | §28 | Signed revision extraction and comparison with the current document (engine and CLI) | §9, D03 T01 §10 | [ ] |
| 29 | §29 | Trust lists: opt-in download and manual import into a Recto trust store (engine and CLI) | §9 | [ ] |
| 30 | §30 | PDF Binder: combine files with a table-of-contents page and a project file | §2, §6, §19 | [ ] |

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
- [ ] Add `MergeOutlinePolicy` (`WrapPerSource`, `TargetOnly`) to `merge_pdfs`: `WrapPerSource` is the behaviour above; `TargetOnly` keeps only the first (target) source's bookmarks, links and named destinations remapped to their new positions, adds no per-source top-level entries, drops the other sources' outlines (counted in `MergeResult::outlines_dropped`), and exposes `recto-cli merge --outlines wrap|target-only`. Done when: `PageOps.MergeTargetOnlyOutlines` inserts one PDF into another at a position and finds only the target's bookmarks on their moved pages, no dangling page reference, and the dropped count.
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
- [ ] Make header, footer and Bates stamps managed marks: wrap each stamped zone run in `/Artifact << /Type /Pagination /Subtype /Header|/Footer /RectoMarkId (id) >> BDC ... EMC`, record the options (zones, Bates config, font, margins) in `/PieceInfo << /Recto << /Private >> >>` on the page, add `HeaderFooter` and `Bates` to `PageMarkKind`, and teach `list_marks` and `remove_mark` (`§13`) to return and remove them. Done when: `StampWatermark.HeaderFooterBatesManagedMarks` stamps a 10-page file, lists the mark with its options and page set, removes it, and finds no stamp bytes in the raw output with page text and pixels equal to the unstamped source. Cheaper substitute: unmarked text runs that cannot be listed or removed later.
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

## 11. `/Adobe.PubSec` certificate encryption writer: recipients, CMS envelopes, AES-256

Acrobat Pro can encrypt a PDF for named people so that only the holders of their certificates can open it, with permissions that differ per person (Protect, Encrypt with Certificate). Recto matches that with the public-key security handler of ISO 32000-2 section 7.6.5 (`/Filter /Adobe.PubSec`), which sits beside the password handler of `§7` and shares its `AES-256` cipher through Windows CNG. `PubSecEncryptor` (`src/pdf/pubsec.h/.cpp`, `recto::pdf::pubsec`) takes recipient certificates from the Windows certificate store or from `.cer`, `.crt` and `.p7b` files, wraps a random seed and each recipient's own permission word in a CMS enveloped-data blob per recipient with crypt32 (`CryptMsgOpenToEncode`), derives the file key exactly as the specification defines for the `AESV3` crypt filter, and encrypts strings and streams with `AES-256` through `bcrypt.dll`. It runs on an isolated copy and writes through `AtomicFileWriter` (`D04 T02 §1`). The rule that matters most: it never falls back. A bad recipient, an unsupported key, a crypt32 or CNG failure, or an empty recipient list is a typed refusal with no output file; it never writes an unencrypted copy, never drops a recipient quietly, and never downgrades to `RC4` or `AES-128`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `PubSecRecipient`, `PubSecEncryptionOptions` and `PubSecEncryptionReport` and implement recipient loading and validation from the store and from files. Done when: `PubSec.RecipientValidation` loads a DER, a PEM and a `.p7b` certificate and a store certificate, and refuses an expired, a non-encipherment, an unsupported-key and a duplicate certificate with the reason named, under ASan.
2. Implement `encrypt_pdf_for_recipients` and `recto-cli encrypt-cert`. Done when: `PubSec.EncryptRoundTrip` encrypts a corpus PDF for two test recipients with different permissions and each test key opens it with its own permissions, and `Cli.EncryptCertCommand` passes, under ASan.

- [ ] Define `PubSecRecipient` (`certificate_der`, `thumbprint_sha1`, `subject`, `not_after`, `PdfPermissions permissions` from `§7`), `PubSecEncryptionOptions` (`recipients`, `encrypt_metadata`, `allow_expired_recipients`, `author_cannot_reopen_acknowledged`) and `PubSecEncryptionReport` in `src/pdf/pubsec.h`. Done when: the header compiles cleanly across `recto-pdf`, `recto-core`, `recto-services` and `recto-cli`.
- [ ] Load recipients from the Windows stores (`CertOpenSystemStore` for `AddressBook`, `MY` and `CA`, enumerated with `CertEnumCertificatesInStore`) and from `.cer`, `.crt` (DER or PEM) and `.p7b` files, bounding each file to `1 MiB` and the list to `1000` recipients. Done when: `PubSec.RecipientLoading` loads each source and refuses an oversized file and a 1001st recipient.
- [ ] Validate every recipient before any output: the key-usage extension (when present) allows key encipherment, the key is an RSA key (RSA key transport is the only recipient type written, because the matching decrypt control differs for key agreement); an elliptic-curve or other key is refused with the algorithm named, and the validity period must include today unless `allow_expired_recipients` is set. Merge duplicate thumbprints keeping the more restrictive permissions and say so in the report. Done when: `PubSec.RecipientValidation` covers every refusal including an ECDH certificate, and the merge.
- [ ] Refuse to write unless the author can reopen the file: at least one recipient must have a private key available to the current user (checked with `CryptAcquireCertificatePrivateKey` using `CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG`), or `author_cannot_reopen_acknowledged` is set. Done when: `PubSec.AuthorCanReopenGuard` refuses a recipient set the author holds no key for and accepts it with the acknowledgment.
- [ ] Build each recipient's CMS enveloped-data blob with `CryptMsgOpenToEncode` and `CMSG_ENVELOPED` using `AES-256-CBC` content encryption, carrying the shared random seed (`BCryptGenRandom`) and that recipient's permission word, and place the blobs in the crypt filter's `/Recipients` array. Done when: `PubSec.RecipientBlobs` decodes each blob and finds the seed and the permission word that recipient was given.
- [ ] Write the `/Encrypt` dictionary (`/Filter /Adobe.PubSec`, the sub-filter ISO 32000-2 section 7.6.5 defines for `AESV3` crypt filters, `/V 5`, `/CF` with `/CFM /AESV3`, `/StmF`, `/StrF`, `/EncryptMetadata`), derive the file key from the seed and recipient data as the specification defines for `AESV3`, and encrypt every string and stream with a fresh random IV and `AES-256-CBC` through `bcrypt.dll`, leaving `/Encrypt` and the trailer `/ID` unencrypted. Done when: `PubSec.EncryptRoundTrip` renders pixel-identically to the source after decryption and finds zero plaintext page strings in the raw output. Cheaper substitute: encrypting with a password handler, or an `AES-128` or `RC4` public-key variant.
- [ ] Enforce per-recipient permissions: each recipient's word comes from its `PdfPermissions`, and the file is written so a reader honours the permissions of the recipient who opened it. Done when: `PubSec.PerRecipientPermissions` reads back different print and copy flags for two recipients.
- [ ] Verify before commit: reopen the temp output, confirm `/Encrypt /Filter /Adobe.PubSec`, confirm the raw bytes hold no plaintext, and (when a test key is available) decrypt one page; any failure deletes the temp file and returns the typed error. Never commit an unencrypted or unverified file. Done when: `PubSec.NoSilentFallback` injects a CMS failure, an empty recipient list and a key-derivation fault and finds no output file and a typed error each time.
- [ ] Implement `encrypt_pdf_for_recipients(src_path, src_password, PubSecEncryptionOptions, dst_path, CancelToken)` in `src/core/page_ops.cpp` on an isolated `pdf::Document` with `AtomicFileWriter`, supporting `dst_path == src_path` as `§3` does. Done when: `PubSec.CancelLeavesNothing` aborts mid-write and the source is byte-identical.
- [ ] Extend the security readback of `D02 T03 §9` (`DocumentSecurityInfo`) with `handler = "Adobe.PubSec"`, the recipient count and the cipher. Done when: `PubSec.SecurityReadback` reports them for the output.
- [ ] Implement `recto-cli encrypt-cert <in.pdf> --out <out.pdf> --recipient <file.cer|thumbprint> [--recipient ...] [--permissions <thumbprint>=print,copy,edit,annotate,forms,assemble] [--no-encrypt-metadata] [--allow-expired] [--acknowledge-author-cannot-reopen] [--json]` in `src/cli/cmd_crypto.cpp`, with no flag that produces an unencrypted file. Done when: `Cli.EncryptCertCommand` verifies exit codes and the refusals.
- [ ] Add `PubSec.*` tests in `tests/recto-tests/pubsec_tests.cpp` using certificates and keys created at test time (`CertCreateSelfSignedCertificate` with NCrypt keys deleted afterwards), committing no key material. Done when: `recto-tests.exe --gtest_filter=PubSec.*:Cli.EncryptCertCommand` passes under ASan.
- [ ] Commit: `organize: certificate encryption with the Adobe.PubSec handler and AES-256`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PubSec.*:Cli.EncryptCertCommand` passes under ASan with the refusal cases and the per-recipient permission readback quoted.

## 12. `/Adobe.PubSec` decryption with a certificate private key through CNG on open

Open and decrypt files produced by `§11` (and by other readers' certificate encryption) with the user's own certificate and private key. `pdf::Document::open` returns a typed `PdfErrorCode::CertificateRequired` carrying the recipient hints (issuer and serial number, or subject key identifier) instead of `PasswordRequired`; the app then supplies a certificate and the `DocumentActor` (`D01 T02 §8`) unwraps its recipient blob with crypt32 (`CryptMsgOpenToDecode`, `CryptMsgControl` with `CMSG_CTRL_DECRYPT`) using the private key through CNG (`CryptAcquireCertificatePrivateKey` returning an `NCRYPT_KEY_HANDLE`), derives the file key, and decrypts strings and streams. The private key operation, which may prompt for a smart-card or token PIN, runs on the actor thread and never on the UI thread, so the window stays responsive. The recipient's permission word becomes the document's enforced permissions. A wrong certificate, a missing key, a cancelled PIN or a failed unwrap is a typed refusal; the engine never renders ciphertext as if it were a page.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PubSecDecryptor` and the `CertificateRequired` open path, and match recipient blobs to candidate certificates. Done when: `PubSec.OpenWithCertificate` opens a `§11` output with the matching test key, refuses a non-recipient key with `NoMatchingCertificate`, and renders the pages pixel-identically to the source, under ASan.
2. Add hostile-input bounds, fuzzing, `decrypt_pdf` for certificates and `recto-cli decrypt-cert`. Done when: `PubSec.HostileRecipients`, `PubSec.DecryptCopy` and `Cli.DecryptCertCommand` pass under ASan and a 60 second `fuzz_pubsec_open` smoke run is clean.

- [ ] Add `PdfErrorCode::CertificateRequired`, `NoMatchingCertificate`, `PrivateKeyUnavailable`, `PinCancelled` and `DecryptionFailed` to `src/pdf/error.h`, and the `PubSecRecipientHint` list (issuer distinguished name and serial number, or subject key identifier) on `CertificateRequired`. Done when: opening a public-key encrypted file returns `CertificateRequired` with one hint per recipient and never `PasswordRequired`.
- [ ] Extend `DocumentActor` (`src/core/document_actor.h/.cpp`) with `supply_certificate(CertContext, hwnd_for_pin_prompt)` beside the password path, keeping the thread contract of `D01 T02 §8`: the private key operation runs on the actor thread, the `CertContext` is duplicated into the actor, and the key's window handle is set through `NCRYPT_WINDOW_HANDLE_PROPERTY` so a PIN prompt is parented to the app. Done when: `PubSec.ActorThreadContract` shows the UI thread never calls the key operation and a blocked PIN prompt does not freeze other documents.
- [ ] Implement `PubSecDecryptor` in `src/pdf/pubsec.cpp`: dispatch on the recipient type of each `/Recipients` blob, match key-transport recipients to the supplied certificate by issuer and serial number or subject key identifier and unwrap with `CryptMsgControl(CMSG_CTRL_DECRYPT)` using an `NCRYPT_KEY_HANDLE`, return `PdfErrorCode::UnsupportedRecipientType` naming it for a key-agreement or other recipient (so a file from another reader is refused clearly, not mis-decrypted), read the seed and permission word, derive the file key as `§11` does, and map crypt32 and CNG failures to the typed codes. Done when: `PubSec.OpenWithCertificate`, `PubSec.KeyFailureMapping` and `PubSec.UnsupportedRecipientType` cover the match, a missing key, a cancelled PIN, a removed token and a key-agreement blob.
- [ ] Check the key against content, not structure: dictionaries are not encrypted, so after deriving the key take a bounded sample of up to `8` encrypted objects chosen in this order from whatever exists (content streams of the first pages that have `/Contents`, then other encrypted streams such as images and fonts, then encrypted strings in `/Info` and the catalog), require each sampled `AES-CBC` object to decrypt with valid padding and each sampled `FlateDecode` stream to inflate with a valid checksum, and return `DecryptionFailed` if any sample fails. Because CBC offers no authentication this is a probabilistic sanity check that a wrong or corrupted key will very probably fail, never proof of integrity. When the document has no encrypted object at all (a blank, empty-content file), there is nothing to sample: the open succeeds with `DocumentDescription::key_unverified = true` and the viewer renders it normally, and the report says the key could not be checked. Done when: `PubSec.WrongKeyNeverRendersGarbage` supplies a certificate that unwraps a blob whose seed was corrupted (catalog and page tree still parse) and gets `DecryptionFailed`, a correct key passes, `PubSec.BlankFirstPage` opens a file whose first page has no `/Contents` and whose encrypted content is on a later page using that sample, and `PubSec.NoEncryptedObjects` opens a file with no encrypted object and sets `key_unverified`.
- [ ] Apply the recipient's permission word as the document's permissions so print, copy and modify controls follow it, using the same enforcement path as the password handler. Done when: `PubSec.PermissionsEnforced` opens a file as a no-print recipient and the print command reports it disabled.
- [ ] Bound hostile input: at most `1000` recipients and `1 MiB` per blob checked before any crypt32 call, a typed refusal on a malformed `/Encrypt` dictionary, and iterative parsing. Done when: `PubSec.HostileRecipients` refuses a 100000-recipient array, an oversized blob and a malformed dictionary without crash or allocation growth.
- [ ] Add a libFuzzer target `fuzz_pubsec_open` in `fuzz/fuzz_pubsec_open.cpp` that mutates the `/Encrypt` dictionary and `/Recipients` blobs of a `§11` output. Done when: a 60 second smoke run (`D01 T03 §5` CI mode) finds no crash, hang or unbounded allocation.
- [ ] Implement `decrypt_pdf_with_certificate(src_path, CertContext, dst_path, CancelToken)` in `src/core/page_ops.cpp` writing a clean unencrypted copy through `GraftMap`, `PdfWriter` and `AtomicFileWriter`, plus `recto-cli decrypt-cert <in.pdf> --certificate <thumbprint> --out <out.pdf> [--no-prompt]`, where `--no-prompt` fails with `PrivateKeyUnavailable` instead of showing a PIN dialog. Done when: `PubSec.DecryptCopy` and `Cli.DecryptCertCommand` pass.
- [ ] Add `PubSec.*` tests for this section to `tests/recto-tests/pubsec_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PubSec.*:Cli.DecryptCertCommand` passes under ASan.
- [ ] Commit: `organize: open and decrypt Adobe.PubSec files with a certificate private key through CNG`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PubSec.*:Cli.EncryptCertCommand:Cli.DecryptCertCommand` passes under ASan.

## 13. Page backgrounds engine: add, update, list and remove managed marks

Acrobat Pro lets a person add a background (a colour, an image or a PDF page), change it later and remove it, and remove watermarks too. Removing a watermark or background later is a common need. `src/core/page_background.h/.cpp` (`recto::core::PageMarks`) adds backgrounds as managed marks: each mark is one Form XObject whose settings live in the object itself (`/PieceInfo` private data holding the mark id, kind, name and options), placed by a content stream inserted at the front of the page's `/Contents` array inside an `/Artifact` marked-content sequence that carries the mark id, so the original content streams stay byte-identical and tagged documents treat the background as an artifact. Because the record lives in the stored content, listing, editing and removing later read it back from the file and cannot drift from what the pages show. `PageMarks` also lists and removes watermarks it can recognise (the `WatermarkFm` Form XObject of `§5` and `/Artifact` sequences declaring `/Subtype /Watermark`), but does not edit them. All work is on an isolated copy and saved as a full rewrite, so a removed mark leaves no bytes behind.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the option and mark types and implement `add_background` with the three sources and four fit modes. Done when: `PageMarks.AddColourImagePdfPage` adds a colour, a PNG and a PDF-page background to a multi-page corpus PDF with the original `/Contents` streams byte-identical and the background painted beneath page content, under ASan.
2. Implement `list_marks`, `update_mark`, `remove_mark`, the preview display list and `recto-cli background`. Done when: `PageMarks.ListUpdateRemove`, `PageMarks.PreviewMatchesSaved` and `Cli.BackgroundCommand` pass under ASan.

- [ ] Define `PageMarkKind` (`Background`, `Watermark`), `BackgroundSource` (`Colour`, `Image`, `PdfPage`), `BackgroundFit` (`Fill`, `Fit`, `Centre`, `Tile`), `BackgroundOptions` (`name`, `source`, `colour_rgba`, `image_or_pdf_path`, `source_page_index`, `fit`, `scale_percent`, `offset_x_pt`, `offset_y_pt`, `rotation_deg`, `opacity`, `target_box` of `CropBox` or `MediaBox`, `scope_filter`, `page_indices`) and `ManagedMark` (`mark_id`, `kind`, `name`, `options`, `page_indices`, `editable`) in `src/core/page_background.h`. Done when: the header compiles cleanly across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement `add_background` in `src/core/page_background.cpp`: build one Form XObject per distinct page box size (colour fill, image with `/SMask` alpha, or a grafted PDF page through `GraftMap`, `§1`), store `/PieceInfo << /Recto << /Private << /MarkId /Kind /Name /Options >> >> >>` on it, and insert a new first stream `/Artifact << /RectoMarkId (id) >> BDC q ... /Bg1 Do Q EMC` into each in-scope page's `/Contents` array, converting a single stream to an array without rewriting it. Done when: `PageMarks.AddColourImagePdfPage` confirms byte-identical original streams and the `/Artifact` wrapper (so a tagged document's structure tree is untouched).
- [ ] Implement the four fit modes, `scale_percent`, offsets, rotation and `opacity` (through an `/ExtGState` `/ca`) against the chosen box, tiling by a repeated placement. Done when: `PageMarks.FitModes` renders each mode on portrait and landscape pages with the expected placement.
- [ ] Detect pages whose own content starts with an opaque full-box fill that would hide the background, apply the mark anyway, and return those page numbers in `BackgroundResult::occluded_pages` so the caller can warn; never rewrite the page's own fill. Done when: `PageMarks.OccludedPagesReported` lists the pages of a fixture with a full-page white rectangle.
- [ ] Keep backgrounds beneath every other layer: teach `apply_watermark` (`§5`) to insert its behind-content stream after any managed background streams, and make `add_background` insert before any existing watermark stream. Done when: `PageMarks.LayerOrderWithWatermark` renders the expected order for background-then-watermark and watermark-then-background.
- [ ] Implement `list_marks(doc)` enumerating every element of each page's `/Contents` array and reading the first `256` bytes of each stream for a `/RectoMarkId` wrapper (cost linear in the number of content streams), so several backgrounds on one page are all listed, and recognising `WatermarkFm` and `/Artifact /Subtype /Watermark` sequences as `Watermark` marks with `editable = false`. Done when: `PageMarks.ListUpdateRemove` lists two backgrounds on the same page, saved and reopened, plus a `§5` watermark, with the right page sets.
- [ ] Implement `update_mark(mark_id, options)` rebuilding the Form XObject and moving the mark to a changed page scope (removing it from pages that leave scope and adding it to pages that enter), keeping the same `mark_id`. Done when: `PageMarks.ListUpdateRemove` changes a colour and a page range and finds the earlier version gone from every page.
- [ ] Implement `remove_mark(mark_id)` removing that mark's inserted stream and its own uniquely named `/Resources /XObject` entry (`RectoBg_<id8>` rather than a shared `/Bg1`), deleting the Form XObject and other objects only when a reference scan finds nothing else uses them, and for recognised watermarks removing the `q /WatermarkFm Do Q` sequence or the `/Artifact` marked-content sequence located through the shared interpreter (`D06 T03 §4`). Done when: `PageMarks.RemoveLeavesNoBytes` removes a background and a watermark and finds no mark bytes in the raw output with page text and pixels equal to the unmarked source, and `PageMarks.RemoveOneOfTwo` removes one of two backgrounds on a page and leaves the other and any shared image intact.
- [ ] Implement `build_background_preview_display_list(page_dl, options)` returning the page's real `DisplayList` with the background composited beneath it, within `5 ms`, without writing a file, so a dialog preview shows true page content. Done when: `PageMarks.PreviewMatchesSaved` renders identical pixels to the saved page.
- [ ] Save the result through `AtomicFileWriter` as a full rewrite on a copy, refuse an encrypted source without its password (`PasswordRequired`), honour `CancelToken`, and never touch the open viewer document. Done when: `PageMarks.CopySemanticsAndCancel` shows the source unchanged after success, failure and cancel.
- [ ] Implement `recto-cli background <add|update|list|remove> <in.pdf> [--colour <rrggbb>] [--image <path>] [--pdf <path> --pdf-page <n>] [--fit fill|fit|centre|tile] [--opacity <0..1>] [--pages <range>] [--id <mark_id>] --out <out.pdf> [--json]` in `src/cli/cmd_background.cpp`. Done when: `Cli.BackgroundCommand` verifies each verb and `--json` listing.
- [ ] Add `PageMarks.*` tests in `tests/recto-tests/page_marks_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PageMarks.*:Cli.BackgroundCommand` passes under ASan.
- [ ] Commit: `organize: managed page backgrounds with list, update and remove, and watermark removal`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageMarks.*:Cli.BackgroundCommand` passes under ASan with the byte-identical-streams and no-bytes-after-remove checks quoted.

## 14. `replace_pages`: replace pages from another PDF, remapping links, fields and tags

Acrobat Pro replaces pages of one PDF with pages of another and keeps the document consistent; a replace must never also replace the gap between non-adjacent pages. `replace_pages` (`src/core/page_ops.h/.cpp`) takes any set of destination pages, including non-adjacent ones, and a sequence of source pages, and builds a copy in which only the chosen pages change. Pages in the gaps are untouched. The destination keeps its catalog (outlines, named destinations, page labels, form, optional content, metadata, viewer preferences) and every reference that pointed at a replaced page is remapped to its replacement or removed on purpose, so the output has no dangling page reference, no orphan form field and no tag pointing at a removed page. It builds on `GraftMap` (`§1`) and the copy rules of `§3`, never edits the open viewer document, and reports what it changed and what it could not carry over.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `ReplacePagesOptions` and `ReplacePagesResult`, validate up front, and build the replaced page list with `GraftMap` from two sources. Done when: `ReplacePages.NonContiguousGapUntouched` replaces pages 2 and 6 of a 10-page PDF and finds pages 3 to 5 pixel-identical to the source, under ASan.
2. Remap or remove every page-bound structure and verify references. Done when: `ReplacePages.OutlinesLinksDests`, `ReplacePages.FormFields`, `ReplacePages.TagsAndLabels` and `ReplacePages.NoDanglingReferences` pass under ASan.

- [ ] Define `ReplaceMode` (`OneToOne`, `RangeToRange`), `OutlinePolicy` (`RemapToReplacement`, `RemoveEntries`), `ReplacePagesOptions` (`target_pages`, `source_path`, `source_password`, `source_pages`, `mode`, `outline_policy`, `dry_run`) and `ReplacePagesResult` (`pages_replaced`, `net_page_change`, `outline_entries_remapped`, `outline_entries_removed`, `links_remapped`, `links_neutralised`, `fields_removed`, `fields_imported`, `fields_renamed`, `pages_untagged`, `xfa_dropped`) in `src/core/page_ops.h`. Done when: the header compiles cleanly across `recto-core`, `recto-services` and `recto-cli`, and `ReplacePages.DryRun` returns the same `ReplacePagesResult` counts as a real run while writing no file.
- [ ] Validate before touching disk with `parse_page_range_spec` (`§1`): targets and sources in range and not empty, `OneToOne` counts equal (else `InvalidArgument` naming both counts), and a non-empty result. Allow the source to be the same file by opening it as a separate read-only instance. Done when: `ReplacePages.Validation` refuses mismatched counts, out-of-range pages and an all-pages `RangeToRange` that would leave zero pages.
- [ ] Build the output through `PdfWriter` with one `GraftMap` per source: kept destination pages in order, each replaced slot filled from the source (`OneToOne`), or the target pages removed and the whole source sequence inserted at the position of the first target (`RangeToRange`). Done when: `ReplacePages.NonContiguousGapUntouched` and `ReplacePages.RangeToRangeCountChange` pass pixel comparison (`expect_rgba_equal`). Cheaper substitute: deleting the pages between the first and last target, or rasterizing the replacement pages.
- [ ] Keep the destination catalog: carry over `/Outlines`, `/Dests` and `/Names` trees, `/PageLabels`, `/AcroForm`, `/OCProperties`, `/Metadata`, `/ViewerPreferences` and `/Lang`, grafting them with the page-reference remap table. Done when: `ReplacePages.CatalogKept` finds each present in the output.
- [ ] Remap page references through the table (kept page to itself, replaced page to its replacement in the same slot or, for `RangeToRange`, to the first inserted page) in outline items, named destinations, link annotations on kept pages, `/GoTo` actions and `/OpenAction`; with `RemoveEntries`, delete outline entries that pointed at replaced pages and promote their children; convert an explicit `/XYZ` position outside the replacement page's `/CropBox` to `/Fit`; and neutralise links whose target is gone. Done when: `ReplacePages.OutlinesLinksDests` covers both policies and `ReplacePages.NoDanglingReferences` scans every `/Dest`, `/D`, `/A`, `/OpenAction` and bookmark and finds each page reference resolves to a page in the new tree.
- [ ] Handle form fields: remove from `/AcroForm /Fields` every widget on a replaced page (deleting parent fields left with no kids and their `/CO` entries), import the fields of widgets on source pages with their ancestors, merge the `/DR` fonts their `/DA` strings need, rename a clashing fully qualified name deterministically (`name_2`) and report it, and drop a stale `/XFA` entry (`xfa_dropped = true`) because Recto never executes XFA (D9). Done when: `ReplacePages.FormFields` replaces a form page with a revised form page and finds exactly the new fields, no orphan widgets and a reported rename.
- [ ] Merge optional-content groups used by imported pages into `/OCProperties /OCGs` and `/D`, and remove article-thread beads that sit on replaced pages. Done when: `ReplacePages.OptionalContentAndThreads` shows imported layers toggle in the output and no bead references a removed page.
- [ ] Define tagged-document handling explicitly: every structural association of an imported object is removed on import, not only the page key, meaning the page `/StructParents`, each imported annotation and widget `/StructParent`, each imported Form XObject and image XObject `/StructParents` or `/StructParent`, and the `/Pg`, `/K` and `/StructParent` links that point at the source tree, and the source structure tree is not imported (so no imported key can collide with a retained entry); the count of affected pages goes to `pages_untagged`. From the destination tree, remove every marked-content and object reference whose page or object belongs to a replaced page, including such kids of elements that also hold kids on retained pages (the element stays if it keeps any kid), then remove elements left with no kids, and rebuild the `/ParentTree` and `/ParentTreeNextKey` by keeping every entry whose owner (retained page, retained annotation, retained XObject or content stream) is retained and dropping only the entries owned by removed objects. Also rebuild `/PageLabels` after a count change (labels after the insertion shift by the net change, inserted pages continue the first replaced page's label style). Done when: `ReplacePages.TagsAndLabels` replaces a page of a tagged corpus PDF that has a mixed-page element, retained tagged widgets and a retained tagged Form XObject, using a tagged source whose page, widget and Form XObject keys equal retained keys, and finds no `/Pg` or `MCR` pointing at a removed page, every retained page, widget and Form XObject `/ParentTree` lookup still resolving to its own element, no imported object carrying a structure key, and correct labels.
- [ ] Verify before commit with a reference scan (`verify_page_references`) and a reopen of the temp output checking page count and field tree, commit through `AtomicFileWriter` (`D04 T02 §1`) on a copy with `dst_path == src_path` supported as in `§3`, handle `PasswordRequired` for either file naming the file, and honour `CancelToken`. Done when: `ReplacePages.CopySemanticsAndCancel` shows the source untouched on success, failure and cancel, and same-path replacement is atomic as `PageOps.ReplaceOriginalAtomic` proves for `§3`.
- [ ] Implement `recto-cli pages replace <in.pdf> --pages <spec> --with <source.pdf> --source-pages <spec> --out <out.pdf> [--mode one-to-one|range] [--bookmarks remap|remove] [--password <pw>] [--source-password <pw>] [--json]` in `src/cli/cmd_page_ops.cpp`, printing the `ReplacePagesResult` with `--json`. Done when: `Cli.PagesReplaceCommand` verifies exit codes, refusals and the result fields.
- [ ] Add `ReplacePages.*` tests and the form, tagged and bookmarked fixtures (`scripts/build-corpus.py`, `tests/corpus/PROVENANCE.md`) in `tests/recto-tests/replace_pages_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ReplacePages.*:Cli.PagesReplaceCommand` passes under ASan.
- [ ] Commit: `organize: replace pages from another PDF with consistent bookmarks, links, fields and tags`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ReplacePages.*:Cli.PagesReplaceCommand` passes under ASan with the gap-untouched pixel comparison and the no-dangling-reference scan quoted.

## 15. Split by maximum file size and by top-level bookmarks (engine and CLI)

Acrobat Pro's Split Document, Foxit PDF Editor's Split, PDFelement's Split and UPDF's Split can all cut a PDF by a maximum file size and by top-level bookmarks, which `§2` does not do (it splits by explicit ranges, every `N` pages and one file per page). This section adds both as `SplitMethod` values on the existing `split_pdf` so they share its graft, atomic-write and cancel behaviour. Each part is a copy written through `GraftMap` (`§1`) and `AtomicFileWriter` (`D04 T02 §1`) on an isolated `pdf::Document`, so the source is never touched, and the plan is computed before any file is created so a refusal leaves nothing on disk. The Recto improvement kept: bookmark titles become file names through a Windows-safe, Unicode-preserving name sanitizer (non-Latin titles stay readable, `D06 T03 §8`), and a part that cannot meet the size limit is reported, never silently split mid-page or dropped.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `MaxFileSize` and `TopLevelBookmarks` to `SplitMethod`, implement `plan_split_by_size` and `plan_split_by_bookmarks` returning a `SplitPlan` (the parts, their page lists and warnings) before any write. Done when: `SplitBySize.PlanGreedy` and `SplitByBookmarks.PlanFromOutline` produce the expected page lists for the corpus fixtures without creating a file.
2. Execute a `SplitPlan` through the existing `split_pdf` part writer and add the CLI options. Done when: `SplitBySize.RoundTripPixels`, `SplitByBookmarks.RoundTripPixels` and `Cli.SplitBySizeAndBookmarks` pass under ASan.

- [ ] Define `SplitPlan` (`parts` of `SplitPartSpec` plus `title`, `warnings`), `SplitWarning` (`OversizePart`, `BookmarksOutOfOrder`, `BookmarkUnresolved`, `DuplicateTitleRenamed`, `FrontMatterPart`) and `FrontMatterPolicy` (`SeparatePart`, `AttachToFirst`, `Drop`) in `src/core/page_ops.h`, extending `SplitMethod`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli` and `PageOps.SplitMethodEnumsStable` pins the enum values.
- [ ] Implement `parse_size_spec(std::string_view)` accepting `500KB`, `2.5MB`, `1GB` as 1024-based units as Windows Explorer shows them, and `KiB`, `MiB`, `GiB` as the same values, refusing zero, negatives, text and any limit below `1 KiB` with `PdfErrorCode::InvalidArgument` naming the token. Done when: `SplitBySize.SizeSpecParsing` covers each unit and every refusal.
- [ ] Implement `plan_split_by_size(doc, max_bytes, CancelToken)`: estimate each page's incremental cost with a `GraftMap` dry run that counts the stream bytes and dictionary overhead of objects not yet in the current part, grow the part page by page, then write the candidate part to a temp stream and measure the real length; when the real length exceeds the limit, shrink by binary search with at most `ceil(log2(n)) + 2` writes per part. Done when: `SplitBySize.PlanGreedy` splits a 40-page fixture with shared fonts into parts that are each at or below the limit and uses no more than the stated writes per part. Cheaper substitute: dividing the source file size by the limit and splitting by equal page counts, which exceeds the limit whenever pages differ in weight.
- [ ] Treat shared resources honestly: every part carries its own copy of the fonts and images its pages use, so part sizes are not additive, and a single page that alone exceeds the limit is written as its own part with an `OversizePart` warning naming the page and its size instead of being refused or cut. Done when: `SplitBySize.OversizeSinglePage` writes the oversize page alone, reports the warning, and `--strict-size` on the CLI exits non-zero with no files written.
- [ ] Implement `plan_split_by_bookmarks(doc, level, FrontMatterPolicy)`: take the outline nodes at depth `level` (`1` is top-level) from `D01 T01 §10`, resolve each destination to a page index, start each part at its bookmark's page and end it before the next part's start page, sort nodes by page when the outline is not in page order (`BookmarksOutOfOrder`), merge nodes that resolve to the same page keeping the first title, skip unresolvable destinations (`BookmarkUnresolved`), and apply the front-matter policy to pages before the first bookmark. Done when: `SplitByBookmarks.PlanFromOutline` covers out-of-order, same-page, unresolvable and front-matter cases and every page lands in exactly one part (or is dropped only under `Drop`).
- [ ] Implement `sanitize_part_file_name(title, used_names)`: replace `<>:"/\|?*` and control characters with `_`, strip trailing dots and spaces, prefix `_` to reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`) with or without an extension, cut at `100` UTF-16 code units without splitting a surrogate pair, keep non-Latin text unchanged, append ` (2)`, ` (3)` for case-insensitive duplicates, and fall back to `part-N` for an empty result. Done when: `SplitByBookmarks.FileNameSanitizer` passes the table of hostile, reserved, duplicate, emoji and CJK titles.
- [ ] Carry each part's own bookmark subtree: remap the children of the part's bookmark and any destinations inside the part with the link remap of `§1`, and drop outline entries whose pages are in other parts. Done when: `SplitByBookmarks.PartOutlinesRemapped` finds each part's outline pointing only at pages inside that part.
- [ ] Execute the plan through `split_pdf`'s part writer (`GraftMap`, `PdfWriter`, `AtomicFileWriter`), validating all names and page lists up front, writing no part when any name collides with an existing file unless the caller asked to overwrite, and rolling back already-written parts on `CancelToken` as `§2` does. Done when: `SplitBySize.RoundTripPixels` and `SplitByBookmarks.RoundTripPixels` match the source pages pixel for pixel (`expect_rgba_equal`) and `SplitBySize.CancelRollsBack` leaves no part file.
- [ ] Implement `recto-cli split <in.pdf> --out-dir <dir> [--max-size <size> [--strict-size] | --by-bookmarks [--level <n>] [--front-matter separate|attach|drop]] [--json]` in `src/cli/cmd_page_ops.cpp`, registered in the `recto-cli` verb table (`D01 T03 §1`), printing the plan with `--dry-run`. Done when: `Cli.SplitBySizeAndBookmarks` verifies exit codes, `--dry-run` writing nothing, and the JSON plan.
- [ ] Add `SplitBySize.*` and `SplitByBookmarks.*` tests and the fixtures (a heavy-page document, an out-of-order outline, a CJK-titled outline) to `tests/recto-tests/split_methods_tests.cpp`, `scripts/build-corpus.py` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=SplitBySize.*:SplitByBookmarks.*:Cli.SplitBySizeAndBookmarks` passes under ASan.
- [ ] Commit: `organize: split by maximum file size and by top-level bookmarks`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SplitBySize.*:SplitByBookmarks.*:Cli.SplitBySizeAndBookmarks` passes under ASan, with the oversize-page warning and the hostile-title table quoted.

## 16. Split one page into several: grid or custom cut lines (engine and CLI)

UPDF's Split Page, PDFelement's and Foxit PDF Editor's page division cut one page into several pages, which is how a two-page scan spread or an oversize sheet becomes separate pages; Acrobat Pro can only crop. `split_page` (`src/core/page_split.h/.cpp`) takes a page, a `rows x cols` grid or explicit horizontal and vertical cut lines in displayed-page points, and writes each cell as its own page. A page box alone only hides content, so by default the engine also removes the text, paths and image pixels that lie outside each cell (`Redactor::strip_outside_regions`, added here), keeping Recto's true-removal rule: a piece does not secretly carry the other pieces. Work is on an isolated copy through `GraftMap` (`§1`) and `AtomicFileWriter`, bookmarks, links and named destinations of the split page are remapped with the reference table of `§14`, and the open viewer document is never modified.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `PageSplitSpec`, compute cell rectangles in user space for any `/Rotate` and implement `split_page` with the keep-outside option. Done when: `PageSplit.GridAndCutLines` splits a rotated and an unrotated fixture into the expected cell boxes and renders each cell pixel-identical to the matching region of the source render.
2. Add true removal outside each cell, annotation and form assignment, reference remapping and `recto-cli split-page`. Done when: `PageSplit.NoContentOutsideCell`, `PageSplit.AnnotationsAndFields`, `PageSplit.ReferencesRemapped` and `Cli.SplitPageCommand` pass under ASan.

- [ ] Define `PageSplitSpec` (`rows`, `cols`, `vertical_cuts_pt`, `horizontal_cuts_pt` measured from the top-left of the displayed page, `column_order` of `LeftToRight` or `RightToLeft`, `page_indices`, `remove_content_outside_cells`) and `PageSplitResult` (`cells_written`, `net_page_change`, `annotations_dropped`, `widgets_assigned`, `content_removed_outside`) in `src/core/page_split.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Validate before writing: cut lines strictly inside the displayed page and at least `4 pt` apart, grid at most `10 x 10`, not both a grid and explicit cuts, and a non-empty page set, refusing with `PdfErrorCode::InvalidArgument` naming the offending value. Done when: `PageSplit.Validation` covers every refusal and no file is created.
- [ ] Convert displayed-page cuts to user-space cell rectangles for `/Rotate` `0`, `90`, `180` and `270` and for a non-zero `/CropBox` origin, order cells top to bottom and left to right (or right to left), and write each cell as a new page grafting the original content once with `/MediaBox` and `/CropBox` set to the cell rectangle and `/Rotate` preserved. Done when: `PageSplit.GridAndCutLines` passes for all four rotations.
- [ ] Add `Redactor::strip_outside_regions(page, cell_rect)` to `src/pdf/redactor.cpp` by adding a `draw_burn_in` flag (default `true`) to `RedactionMark` and calling the glyph-run splitter, vector clip and image pixel scrubber of `§10` on the four strips of the page box outside the cell with `draw_burn_in = false`. Done when: `PageSplit.NoContentOutsideCell` finds no text, path or image pixel outside each cell in a raw stream scan and `PageText` of each cell holds only that cell's words. Cheaper substitute: setting only `/CropBox`, which leaves every other cell's text extractable.
- [ ] Allow `remove_content_outside_cells = false` for callers that want a box-only split, and set `content_removed_outside = false` in the result so the CLI and UI can state that the other cells' content remains in the file. Done when: `PageSplit.KeepOutsideReported` shows the flag and the retained text.
- [ ] Assign annotations by rectangle: keep a link or markup annotation in every cell its `/Rect` intersects, assign each form widget to the single cell containing its centre (dropping it from the others so field names stay unique), and rewrite `/Rect` and `/QuadPoints` unchanged because user space is unchanged. Done when: `PageSplit.AnnotationsAndFields` finds each widget on exactly one cell and links on every cell they cross.
- [ ] Remap bookmarks, link destinations and named destinations of the split page to its first cell and the page labels through `remap_page_labels` (`§19`) using the reference table of `§14`, and run `verify_page_references` before commit. Done when: `PageSplit.ReferencesRemapped` finds no dangling page reference and the net page count equals `cells - 1` per split page.
- [ ] Commit through `AtomicFileWriter` as a copy (`dst_path == src_path` supported as in `§3`), refuse an encrypted source without its password naming the file, and honour `CancelToken`. Done when: `PageSplit.CopySemanticsAndCancel` shows the source unchanged after success, failure and cancel.
- [ ] Implement `recto-cli split-page <in.pdf> --out <out.pdf> (--grid <RxC> | --cuts-v <pt,...> --cuts-h <pt,...>) [--pages <spec>] [--rtl] [--keep-outside] [--json]` in `src/cli/cmd_page_ops.cpp`. Done when: `Cli.SplitPageCommand` verifies the verbs, refusals and the `--keep-outside` message.
- [ ] Add `PageSplit.*` tests and the fixtures (a two-page scan spread, a rotated page, a page with a widget and a link on the cut) to `tests/recto-tests/page_split_tests.cpp` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=PageSplit.*:Cli.SplitPageCommand` passes under ASan.
- [ ] Commit: `organize: split one page into several with true removal outside each cell`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageSplit.*:Cli.SplitPageCommand` passes under ASan with the raw-scan "nothing outside the cell" result quoted.

## 17. N-up sheets: several pages on one sheet as a new PDF (engine and CLI)

PDFelement's page layout and Foxit PDF Editor's N-up put several pages on one sheet as a saved PDF; Acrobat Pro only does this when printing, and Recto's print layouts live in `D07 T01 §10`. `make_nup` (`src/core/nup.h/.cpp`) builds a new document in which each output sheet places `rows x cols` source pages as Form XObjects grafted once per source page (`GraftMap`, `§1`), so vector text stays vector and a repeated page is stored once. It runs on an isolated copy and never modifies the source. The sheets are flattened by nature: links, bookmarks and tags of the source are not carried, which the result reports, and visible annotation appearances are drawn so comments still show.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `NUpOptions` and implement cell layout, fit scaling and the Form XObject placement with page rotation. Done when: `NUp.TwoUpAndFourUp` lays 2-up landscape and 4-up portrait sheets from a mixed-size fixture and every cell renders the source page scaled to fit.
2. Add annotation appearances, orientation choice, border, ordering, repeat mode and `recto-cli nup`. Done when: `NUp.AnnotationsBordersAndOrder`, `NUp.RepeatPage` and `Cli.NUpCommand` pass under ASan.

- [ ] Define `NUpOptions` (`rows`, `cols`, `sheet` of `A3`, `A4`, `A5`, `Letter`, `Legal`, `Tabloid` or `Custom` with `width_pt x height_pt`, `orientation` of `Auto`, `Portrait` or `Landscape`, `margin_pt`, `gutter_pt`, `order` of `RowsLeftToRight`, `RowsRightToLeft` or `ColumnsTopToBottom`, `border_width_pt`, `border_rgba`, `include_annotations`, `repeat_each_page`, `page_indices`) and `NUpResult` (`sheets_written`, `orientation_used`, `links_dropped`, `tags_dropped`) in `src/core/nup.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Validate `rows` and `cols` between `1` and `8`, margins and gutters that leave a positive cell, and a non-empty page set, with `PdfErrorCode::InvalidArgument` naming the value. Done when: `NUp.Validation` covers every refusal and writes no file.
- [ ] Wrap each source page as a Form XObject through `GraftMap` with `/BBox` equal to the page's `/CropBox` and `/Matrix` applying `/Rotate`, and place it in its cell with `q a b c d e f cm /P<n> Do Q` using a uniform scale `min(cell_w / w, cell_h / h)` centred in the cell. Done when: `NUp.TwoUpAndFourUp` pixel-matches reference renders of a mixed A4, Letter and landscape fixture. Cheaper substitute: rasterizing each page to an image, which loses selectable text and vector sharpness.
- [ ] Resolve `Auto` orientation once per document by choosing the sheet orientation that gives the larger scale for the first selected page, and record it in `orientation_used`. Done when: `NUp.AutoOrientation` picks landscape for a portrait page in a 1x2 grid and portrait for a landscape page in a 2x1 grid.
- [ ] Draw visible annotation appearance streams (`/AP /N`, ISO 32000-2 section 12.5.5 matrix algorithm) inside the placed page when `include_annotations` is set, skipping hidden, no-view and zero-size annotations, and count dropped links and tag trees in the result. Done when: `NUp.AnnotationsBordersAndOrder` shows a highlight and a stamp on the sheet and `links_dropped` equals the source link count.
- [ ] Implement the three cell orders, the optional border rectangle and `repeat_each_page` (the same page in every cell of its sheet, stored once). Done when: `NUp.AnnotationsBordersAndOrder` and `NUp.RepeatPage` verify cell order by text position and that a repeated page adds one Form XObject.
- [ ] Write the result as a new document through `PdfWriter` and `AtomicFileWriter` with `/Info` copied from the source and `/Producer` set by Recto, refuse an encrypted source without its password naming the file, and honour `CancelToken`. Done when: `NUp.CopySemanticsAndCancel` shows the source unchanged after success, failure and cancel.
- [ ] Implement `recto-cli nup <in.pdf> --out <out.pdf> --grid <RxC> [--sheet a4|a3|letter|legal|<WxH>] [--orientation auto|portrait|landscape] [--margin <pt>] [--gutter <pt>] [--order rows|rtl|columns] [--border <pt>] [--repeat] [--pages <spec>] [--json]` in `src/cli/cmd_nup.cpp`. Done when: `Cli.NUpCommand` verifies exit codes and the JSON result.
- [ ] Add `NUp.*` tests and a mixed-size fixture to `tests/recto-tests/nup_tests.cpp` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=NUp.*:Cli.NUpCommand` passes under ASan.
- [ ] Commit: `organize: N-up sheets as a new PDF with vector pages`

**Test checkpoint:** `recto-tests.exe --gtest_filter=NUp.*:Cli.NUpCommand` passes under ASan with the mixed-size pixel comparison quoted.

## 18. Interleave two PDFs, swap odd and even pages, reverse page order (engine and CLI)

UPDF, PDFelement and Foxit PDF Editor offer Interleave, Swap Odd and Even, and Reverse Order for the common two-pass duplex scan: the feeder scans all fronts into one file and, after the stack is flipped, all backs into another in reverse. `§2` already grafts any interleaved selection, so this section adds the named operations on top of `PageEditPlan` (`§3`) and the merge graft (`§2`): `interleave_pdfs`, `swap_odd_even` and `reverse_pages`. All three are copy operations on isolated documents through `AtomicFileWriter`, and none changes the source files.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `reverse_pages` and `swap_odd_even` as `PageEditPlan` builders and `interleave_pdfs` over two `GraftMap`s. Done when: `PageOrder.ReverseSwapInterleave` produces the expected page sequences for odd and even counts and a sub-range, verified by per-page text.
2. Handle unequal counts, outline and label remapping and the CLI verbs. Done when: `PageOrder.UnequalCounts`, `PageOrder.OutlineAndLabels` and `Cli.PagesInterleaveSwapReverse` pass under ASan.

- [ ] Define `InterleaveOptions` (`reverse_b`, `allow_unequal_counts`, `a_pages`, `b_pages`, `password_a`, `password_b`) and `PageOrderResult` (`pages_written`, `unmatched_pages`, `b_outlines_dropped`) in `src/core/page_ops.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement `reverse_pages(src, range)` as a `PageEditPlan` that reverses the chosen contiguous range (or all pages) and leaves pages outside it in place. Done when: `PageOrder.ReverseSwapInterleave` reverses a 7-page file and pages 3 to 6 of a 9-page file correctly.
- [ ] Implement `swap_odd_even(src, range)` swapping pairs `(1,2)`, `(3,4)` and so on inside the range, leaving an unpaired last page in place, and reporting the pair count. Done when: `PageOrder.ReverseSwapInterleave` covers even and odd page counts and a range starting on an even page.
- [ ] Implement `interleave_pdfs(a, b, InterleaveOptions, dst, CancelToken)` building `A1, B1, A2, B2, ...` (with `reverse_b` taking B from its last page to its first), one `GraftMap` per source so shared resources are stored once per source. Done when: `PageOrder.ReverseSwapInterleave` matches the expected order for the duplex scan case (fronts `1,2,3`, backs `3,2,1`) and renders pixel-identical to the manual merge of the same order.
- [ ] Refuse unequal page counts with `InvalidArgument` naming both counts unless `allow_unequal_counts` is set, in which case append the remaining pages of the longer file in order and report them as `unmatched_pages`. Done when: `PageOrder.UnequalCounts` covers both modes and the refusal writes no file.
- [ ] Remap the first source's bookmarks, links and named destinations to the new page positions with the link remap of `§1`, drop the second source's (counted in `b_outlines_dropped`), and carry labels through `remap_page_labels` (`§19`). Done when: `PageOrder.OutlineAndLabels` finds A's bookmarks on the right interleaved pages and no dangling page reference.
- [ ] Surface `PasswordRequired` and `InvalidPassword` with the offending file path for either input, honour `CancelToken` and write through `AtomicFileWriter`. Done when: `PageOrder.EncryptedAndCancel` shows no output file and the source files unchanged.
- [ ] Implement `recto-cli pages interleave <a.pdf> <b.pdf> --out <out.pdf> [--reverse-b] [--allow-unequal]`, `recto-cli pages swap-odd-even <in.pdf> --out <out.pdf> [--pages <spec>]` and `recto-cli pages reverse <in.pdf> --out <out.pdf> [--pages <spec>]` in `src/cli/cmd_page_ops.cpp`, extending the verb family of `§3`. Done when: `Cli.PagesInterleaveSwapReverse` verifies each verb, the refusals and exit codes.
- [ ] Add `PageOrder.*` tests to `tests/recto-tests/page_order_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PageOrder.*:Cli.PagesInterleaveSwapReverse` passes under ASan.
- [ ] Commit: `organize: interleave two PDFs, swap odd and even pages, reverse page order`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageOrder.*:Cli.PagesInterleaveSwapReverse` passes under ASan with the duplex-scan sequence quoted.

## 19. Page labels: read, edit, preview and carry through page operations (engine and CLI)

Acrobat Pro's Page Labels, Foxit PDF Editor's, PDFelement's and UPDF's page-number settings let a person make front matter `i, ii, iii` and the body `1, 2, 3`, or add prefixes such as `A-1`. Recto today only preserves `/PageLabels` (`§14` rebuilds them after a replace) and `§3` does not say what happens to them when pages are reordered, extracted or merged. `PageLabels` (`src/core/page_labels.h/.cpp`) reads the `/PageLabels` number tree with bounded recursion, formats labels exactly as ISO 32000-2 section 12.4.2 defines, writes a normalised flat `/Nums` array on a copy, and supplies `remap_page_labels`, the one function every page operation uses so a page keeps the label it showed. The stored tree is the single source of truth: previews, the viewer and print all read it back from the file.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the range model, read the number tree with bounds and format labels for all styles. Done when: `PageLabels.ReadAndFormat` reads flat and nested trees and formats decimal, upper and lower Roman, and upper and lower letter styles with prefixes.
2. Write labels, add `remap_page_labels`, hook the page operations and add `recto-cli labels`. Done when: `PageLabels.WriteRoundTrip`, `PageLabels.RemapThroughOperations`, `Cli.LabelsCommand` and `fuzz_page_labels` (60 second smoke) pass under ASan.

- [ ] Define `PageLabelStyle` (`None`, `Decimal`, `UpperRoman`, `LowerRoman`, `UpperLetters`, `LowerLetters`), `PageLabelRange` (`first_page_index`, `style`, `prefix`, `start` of at least `1`) and `LabelPolicy` (`KeepLabels`, `Renumber`) in `src/core/page_labels.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement `read_page_labels(doc)` over the `/Catalog /PageLabels` number tree (`/Nums` and `/Kids`) with an iterative walk, depth at most `16`, at most `10000` nodes and `100000` entries, cycle-safe, tolerating unsorted keys and a missing first key at `0`, reading prefixes in PDFDocEncoding or UTF-16BE, and returning `PdfResult<std::vector<PageLabelRange>>` with a typed fault for a hostile tree. Done when: `PageLabels.ReadAndFormat` and `PageLabels.HostileTree` (a cyclic and a 100000-entry tree) pass under ASan.
- [ ] Implement `label_for_page(ranges, page_index)` per ISO 32000-2 Table 159: Roman numerals in additive form with repeated `M` above `3999`, letters `A` to `Z`, then `AA` to `ZZ`, then `AAA` to `ZZZ`, an absent style giving the prefix alone, and a page before the first range using decimal from `1`. Done when: `PageLabels.ReadAndFormat` covers `4`, `9`, `14`, `3999`, `4000`, `26`, `27`, `52` and `53` for the styles.
- [ ] Implement `set_page_labels(src, ranges, dst, CancelToken)` writing a normalised flat `/Nums` array, inserting a decimal range at page `0` when the first range starts later, rejecting ranges that are out of order, duplicated, or beyond the page count, and `clear_page_labels`, always on a copy through `PdfWriter` and `AtomicFileWriter`. Done when: `PageLabels.WriteRoundTrip` writes, reopens and reads back the same ranges and `PageLabels.Validation` covers each refusal.
- [ ] Implement `remap_page_labels(source_ranges, kept_source_indices, policy)` producing the output ranges so that, under `KeepLabels`, output page `k` shows the label source page `kept[k]` showed: consecutive source pages with the same style and prefix and consecutive numbers share one range, anything else starts a new range with an explicit `start`; an inserted blank page gets an empty label with no style; under `Renumber` no ranges are written. Done when: `PageLabels.RemapThroughOperations` keeps `iv` on a moved page and shows the expected ranges for reorder, delete, extract and a blank insert.
- [ ] Hook `remap_page_labels` into `apply_page_edits`, `extract_pages`, `split_pdf`, `merge_pdfs` (each labelled source keeps its labels, an unlabelled segment continues decimal from its physical position when any source has labels, and no labels are written when none do) and the label rebuild of `replace_pages` (`§14`). Done when: `PageLabels.RemapThroughOperations` runs each operation and reads the output labels back, and `ReplacePages.TagsAndLabels` still passes.
- [ ] Add `build_label_preview(ranges, page_count)` returning every page's label string for the dialog and CLI preview, capped at `100000` pages. Done when: `PageLabels.Preview` returns `i, ii, iii, 1, 2, 3` for a front-matter fixture.
- [ ] Add a libFuzzer target `fuzz_page_labels` in `fuzz/fuzz_page_labels.cpp` mutating the number tree and prefix strings. Done when: a 60 second smoke run (`D01 T03 §5` CI mode) finds no crash, hang or unbounded allocation.
- [ ] Implement `recto-cli labels list <in.pdf> [--json]`, `recto-cli labels set <in.pdf> --out <out.pdf> --range <page>:<style>[:<prefix>][:<start>]...` and `recto-cli labels clear <in.pdf> --out <out.pdf>` in `src/cli/cmd_labels.cpp`. Done when: `Cli.LabelsCommand` verifies each verb, the style letters `D r R a A` and the refusals.
- [ ] Add `PageLabels.*` tests and a front-matter fixture to `tests/recto-tests/page_labels_tests.cpp` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=PageLabels.*:Cli.LabelsCommand` passes under ASan.
- [ ] Commit: `organize: page labels engine with remapping through page operations`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageLabels.*:Cli.LabelsCommand` passes under ASan with the Roman and letter table and the moved-page `iv` result quoted.

## 20. Blank page detection with a local heuristic and a deletion plan (engine and CLI)

PDFelement's and UPDF's Delete Blank Pages remove the empty sheets a duplex scanner or a bad export leaves in a file. `detect_blank_pages` (`src/core/blank_pages.h/.cpp`) classifies every page with a deterministic, local, two-stage heuristic and returns the evidence for each verdict so the person can review it; it never deletes anything itself. Deleting is a separate `plan_blank_page_deletion` that produces a `PageEditPlan` for `apply_page_edits` (`§3`), a copy operation. The heuristic is conservative: a page with visible text, an annotation, or enough ink is never called blank, an invisible OCR text layer over a blank scan does not make the page non-blank by default, and the thresholds are settings the caller can change.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the structural pass over the page `DisplayList` and the raster ink-ratio pass with margin and speckle handling. Done when: `BlankPages.Verdicts` classifies the corpus fixtures (truly blank, white text on white, blank scan with edge shadow and speckle, faint watermark, one dot, a signature box) as expected at each sensitivity.
2. Add the annotation and hidden-text policies, the deletion plan and `recto-cli blank-pages`. Done when: `BlankPages.AnnotationAndHiddenTextPolicy`, `BlankPages.DeletionPlan` and `Cli.BlankPagesCommand` pass under ASan.

- [ ] Define `BlankSensitivity` (`Low`, `Medium`, `High`), `BlankDetectOptions` (`sensitivity`, `ignore_margin_percent` default `5`, `annotations_count_as_content` default `true`, `hidden_text_counts_as_content` default `false`, `page_indices`), `BlankVerdict` (`Blank`, `NearlyBlank`, `NotBlank`) and `BlankPageFinding` (`page_index`, `verdict`, `ink_ratio`, `reasons`) in `src/core/blank_pages.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement the structural pass on the `DisplayList` (`D01 T02 §1`): a glyph run with a visible fill (not render mode `3`, non-zero alpha, colour not within a small distance of white), any painted path above a minimum area or length, any placed image or shading, or a non-empty Form XObject that draws any of these marks the page as having content; a page with none of them is `Blank` without rasterizing. Done when: `BlankPages.Verdicts` marks an empty page and a page with only white text on white as `Blank` and a one-word page as `NotBlank`.
- [ ] Implement the raster pass for pages with images or paths: render at `36 DPI` greyscale through the tile rasterizer (`D01 T02 §6`), ignore the outer margin band, erode isolated pixels with a `3 x 3` kernel to drop speckle, and compute the ratio of pixels darker than luminance `200`; thresholds are `0.5%` for `High`, `0.1%` for `Medium` and `0.02%` for `Low` sensitivity, `Blank` at or below the threshold, `NearlyBlank` up to four times it, otherwise `NotBlank`. Done when: `BlankPages.Verdicts` classifies a scanned blank with a dark edge shadow and punch holes as `Blank` at `Medium` and a page with a faint watermark as `NotBlank` at `Low` and `NearlyBlank` at `High`.
- [ ] Apply the annotation policy (a visible annotation, widget with a value, or stamp makes a page `NotBlank` with the reason named) and the hidden-text policy (text in render mode `3` over an otherwise blank page keeps the page blank unless `hidden_text_counts_as_content`, and the finding lists `hidden text present`). Done when: `BlankPages.AnnotationAndHiddenTextPolicy` covers each combination.
- [ ] Bound the work: honour `CancelToken` between pages, skip nothing silently (a page that fails to render is reported `NotBlank` with the reason `render failed`), and cap the raster pass at `4 MP` per page. Done when: `BlankPages.CancelAndRenderFailure` shows both behaviours.
- [ ] Implement `plan_blank_page_deletion(findings, include_nearly_blank, extra_exclusions)` returning a `PageEditPlan` for `apply_page_edits`, refusing with `InvalidArgument` when it would delete every page and never including a page the caller excluded. Done when: `BlankPages.DeletionPlan` deletes only the chosen pages and the output renders the kept pages pixel-identical to the source.
- [ ] Implement `recto-cli blank-pages detect <in.pdf> [--sensitivity low|medium|high] [--annotations-blank] [--hidden-text-content] [--json]` and `recto-cli blank-pages delete <in.pdf> --out <out.pdf> [--sensitivity ...] [--include-nearly-blank] [--keep <spec>]` in `src/cli/cmd_blank_pages.cpp`. Done when: `Cli.BlankPagesCommand` verifies the JSON findings and that `delete` writes only a copy.
- [ ] Add `BlankPages.*` tests and the six fixtures to `tests/recto-tests/blank_pages_tests.cpp`, `scripts/build-corpus.py` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=BlankPages.*:Cli.BlankPagesCommand` passes under ASan.
- [ ] Commit: `organize: blank page detection with evidence and a deletion plan`

**Test checkpoint:** `recto-tests.exe --gtest_filter=BlankPages.*:Cli.BlankPagesCommand` passes under ASan with the per-fixture verdict table quoted.

## 21. Reorder pages by bookmark order (engine and CLI)

Foxit PDF Editor can reorder pages to follow the bookmark order, which fixes a document whose outline was rearranged by hand and whose pages were not. `order_pages_by_bookmarks` (`src/core/page_ops.cpp`) builds a `PageEditPlan` (`§3`) from the outline (`D01 T01 §10`) so the physical order follows the outline's depth-first order: each bookmarked page leads a run made of itself and the unbookmarked pages that followed it in the source, and the runs are laid out in outline order. It is a copy operation, deterministic, and never drops a page.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement run building and the plan. Done when: `OrderByBookmarks.RunsFollowOutline` reorders a fixture whose outline lists chapters `3, 1, 2` into chapters `3, 1, 2` with every page present exactly once.
2. Add depth control, duplicate and unresolved handling and the CLI verb. Done when: `OrderByBookmarks.DepthAndEdgeCases` and `Cli.PagesOrderByBookmarks` pass under ASan.

- [ ] Define `OrderByBookmarksOptions` (`max_depth` default unlimited, `front_pages` of `KeepAtFront`) and `OrderByBookmarksResult` (`runs`, `unresolved_bookmarks`, `duplicate_destinations`, `already_in_order`) in `src/core/page_ops.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Walk the outline depth first to `max_depth`, resolve each destination to a page index, keep the first bookmark for a page that several bookmarks target, and skip unresolvable destinations, counting both. Done when: `OrderByBookmarks.DepthAndEdgeCases` covers duplicates, unresolved destinations and a depth limit of `1`.
- [ ] Build runs in the source order (a bookmarked page plus the following unbookmarked pages up to the next bookmarked page), keep pages before the first bookmarked page at the front, order the runs by the outline sequence, and set `already_in_order` when the plan is the identity. Done when: `OrderByBookmarks.RunsFollowOutline` shows every page exactly once and an identity plan for an ordered fixture.
- [ ] Apply the plan with `apply_page_edits` on a copy, keeping labels through `remap_page_labels` (`§19`) and remapping bookmarks and links through the link remap of `§1`. Done when: `OrderByBookmarks.OutlinePointsAtMovedPages` finds each bookmark on its moved page and no dangling reference.
- [ ] Implement `recto-cli pages order-by-bookmarks <in.pdf> --out <out.pdf> [--depth <n>] [--dry-run] [--json]` in `src/cli/cmd_page_ops.cpp`, where `--dry-run` prints the new order and writes nothing. Done when: `Cli.PagesOrderByBookmarks` verifies the dry run and the written copy.
- [ ] Add `OrderByBookmarks.*` tests and a rearranged-outline fixture to `tests/recto-tests/page_order_tests.cpp` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=OrderByBookmarks.*:Cli.PagesOrderByBookmarks` passes under ASan.
- [ ] Commit: `organize: reorder pages to follow the bookmark order`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrderByBookmarks.*:Cli.PagesOrderByBookmarks` passes under ASan.

## 22. Cross-page paging seal: split a seal image across page edges (engine and CLI)

PDFelement's cross-page ("paging") seal, used for contracts, cuts one seal image into slices and puts one slice on the edge of each page so a stack of pages cannot be swapped without breaking the seal. `add_paging_seal` (`src/core/paging_seal.h/.cpp`) takes a seal image, a page set and an edge, decodes the image through WIC, cuts it into one pixel slice per page, and places slice `k` flush to the chosen edge of the `k`-th page of the set. Each page carries only its own slice as an image XObject (not the whole image clipped), so extracting images from one page never reveals the full seal, matching Recto's true-removal stance. The slices are managed marks of kind `PagingSeal` in the registry of `§13` (an `/Artifact` marked-content sequence with `/RectoMarkId` and the options in `/PieceInfo`), so they can be listed and removed later. It works on an isolated copy and is a visual aid only: it is not a digital signature and the report says so.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `PagingSealOptions`, slice the image and place each slice on its page edge. Done when: `PagingSeal.SlicesAndPlacement` seals a 5-page file with a PNG seal on the right edge and each page holds one slice image at the expected rectangle.
2. Register the marks for list and remove, add other edges and the CLI. Done when: `PagingSeal.ListAndRemove`, `PagingSeal.AllEdges` and `Cli.SealAcrossCommand` pass under ASan.

- [ ] Define `SealEdge` (`Right`, `Left`, `Top`, `Bottom`), `PagingSealOptions` (`image_path`, `edge`, `page_indices`, `seal_width_pt`, `seal_height_pt`, `anchor_percent` along the edge, `opacity`, `layer` of `InFront` or `Behind`) and `PagingSealResult` (`slices_written`, `slice_pixel_sizes`, `mark_id`) in `src/core/paging_seal.h`, and add `PagingSeal` to `PageMarkKind` in `src/core/page_background.h`. Done when: the headers compile across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Validate that the page set is at least `2` pages, the seal is between `10 pt` and the shortest page side, the image decodes through WIC within a `64 MP` limit, and the anchor lies inside the page, refusing with `InvalidArgument` naming the value. Done when: `PagingSeal.Validation` covers each refusal and writes no file.
- [ ] Decode the image to RGBA through WIC, scale it to `seal_width_pt x seal_height_pt` at its native pixel density, cut it into `N` equal pixel slices along the edge's axis (the last slice absorbing the remainder), and write each slice as its own Flate image XObject with an `/SMask` for alpha. Done when: `PagingSeal.SlicesAndPlacement` finds the slice widths sum to the image width and reassembling them reproduces the source pixels.
- [ ] Place slice `k` on the `k`-th page of the set flush to the edge at the anchor, inside an `/Artifact << /RectoMarkId (id) >> BDC q ... Do Q EMC` sequence appended to the page `/Contents` array without rewriting existing streams, with `/ExtGState` opacity when set. Done when: `PagingSeal.AllEdges` renders each edge on portrait, landscape and rotated pages with the slice at the edge.
- [ ] Store the options in `/PieceInfo << /Recto << /Private << /MarkId /Kind /Options >> >> >>` on each slice XObject and teach `list_marks` and `remove_mark` (`§13`) to list and remove `PagingSeal` marks, deleting the slice images with the mark. Done when: `PagingSeal.ListAndRemove` removes a seal and finds no slice bytes in the raw output and page pixels equal to the unsealed source.
- [ ] Write through `AtomicFileWriter` as a full rewrite on a copy, refuse an encrypted source without its password, honour `CancelToken`, and state in the result that the seal is not a digital signature. Done when: `PagingSeal.CopySemanticsAndCancel` shows the source unchanged after success, failure and cancel.
- [ ] Implement `recto-cli seal-across <in.pdf> --out <out.pdf> --image <seal.png> [--edge right|left|top|bottom] [--pages <spec>] [--width <pt>] [--anchor <percent>] [--opacity <0..1>] [--json]` in `src/cli/cmd_seal_across.cpp`. Done when: `Cli.SealAcrossCommand` verifies the options and refusals.
- [ ] Add `PagingSeal.*` tests and a seal PNG fixture to `tests/recto-tests/paging_seal_tests.cpp` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=PagingSeal.*:Cli.SealAcrossCommand` passes under ASan.
- [ ] Commit: `organize: cross-page paging seal with per-page image slices`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PagingSeal.*:Cli.SealAcrossCommand` passes under ASan with the slice-sum and extracted-image checks quoted.

## 23. ID card copy: front and back images on one page (engine and CLI)

UPDF's ID card copy puts the front and back photos of an identity card on one page at real size, the usual form for a photocopy that a bank or an employer asks for. `make_id_card_copy` (`src/core/id_card_copy.h/.cpp`) builds a new one-page PDF from two images through WIC (`D04 T02 §5`), places each at the true card size (ISO/IEC 7810 ID-1, `85.60 x 53.98 mm`, by default) centred on the page one above the other, and keeps the camera's orientation. The image data is embedded as given when it needs no crop or rotation (a JPEG stays a pass-through `DCTDecode` stream), so there is no quality loss. It runs offline, on the files the user names, and writes through `AtomicFileWriter`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `make_id_card_copy` with card-size presets, EXIF orientation and crop. Done when: `IdCardCopy.LayoutAndSize` produces an A4 page with two images at `85.60 x 53.98 mm` centred with the stated gap, from a rotated phone photo and a scan.
2. Add the CLI verb and the lossless embedding check. Done when: `IdCardCopy.JpegPassThrough` and `Cli.IdCardCommand` pass under ASan.

- [ ] Define `CardSizePreset` (`Id1_85_6x53_98mm`, `Id3_125x88mm`, `A6`, `Custom`), `IdCardCopyOptions` (`front_image`, `back_image`, `paper` of `A4`, `Letter` or custom, `card_size`, `gap_mm` default `10`, `border_width_pt`, `front_crop` and `back_crop` as normalised rectangles, `title`) and `IdCardCopyResult` in `src/core/id_card_copy.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Decode both images through WIC with a `64 MP` limit, apply the EXIF orientation, apply the optional normalised crop, and refuse an unreadable or oversize image naming the file. Done when: `IdCardCopy.OrientationAndCrop` renders a rotated phone photo upright and cropped.
- [ ] Lay out the two cards centred horizontally, stacked with the gap, centred vertically as a pair, scaled to the card size preserving aspect ratio and centred in the card box, with an optional hairline border. Done when: `IdCardCopy.LayoutAndSize` checks the placed rectangles to within `0.01 pt`.
- [ ] Embed a JPEG without crop or rotation as a `DCTDecode` pass-through and anything else as Flate RGB with an `/SMask` when it has alpha. Done when: `IdCardCopy.JpegPassThrough` finds the original JPEG bytes in the output stream. Cheaper substitute: re-encoding every image through a bitmap, which degrades the photo.
- [ ] Write through `PdfWriter` and `AtomicFileWriter` with `/Info /Title` set from the option and no source image path or EXIF location data in the output, and honour `CancelToken`. Done when: `IdCardCopy.NoMetadataLeak` finds no GPS tag or file path in the raw output.
- [ ] Implement `recto-cli idcard --front <img> --back <img> --out <out.pdf> [--paper a4|letter|<WxHmm>] [--card id1|id3|a6|<WxHmm>] [--gap <mm>] [--border <pt>]` in `src/cli/cmd_idcard.cpp`. Done when: `Cli.IdCardCommand` verifies the options and refusals.
- [ ] Add `IdCardCopy.*` tests and the image fixtures to `tests/recto-tests/id_card_copy_tests.cpp` and `tests/corpus/PROVENANCE.md` (synthetic cards only, no real identity data). Done when: `recto-tests.exe --gtest_filter=IdCardCopy.*:Cli.IdCardCommand` passes under ASan.
- [ ] Commit: `organize: ID card copy with front and back at true card size`

**Test checkpoint:** `recto-tests.exe --gtest_filter=IdCardCopy.*:Cli.IdCardCommand` passes under ASan with the placed-rectangle and JPEG pass-through checks quoted.

## 24. Whiteout and whole-page redaction (engine and CLI)

Foxit PDF Editor, PDFelement and UPDF offer a whiteout that covers content with an opaque box, and redaction by page range. Acrobat Pro has no whiteout. Recto adds both without blurring the line between them: `whiteout` draws an opaque cover and removes nothing, and its result and every surface say so, while `redact_pages` removes a whole page's content for real. A whiteout is not a redaction: `D06 T03 §6`'s audit is expected to flag Recto's own whiteout as a cover without removal, which is the correct finding. `redact_pages` replaces a page's content with the burn-in only, drops its annotations, thumbnail, private data and now-unreferenced resources, and writes a full rewrite so no earlier revision of the page remains.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `whiteout` and `redact_pages` in `src/pdf/redactor.cpp` with their reports. Done when: `Whiteout.CoverKeepsContentAndSaysSo` and `RedactPages.WholePageRemoved` pass under ASan.
2. Add the CLI options and the audit relationship. Done when: `Cli.WhiteoutAndRedactPages` and `Whiteout.AuditFlagsCover` pass.

- [ ] Define `WhiteoutMark` (`page_index`, `rect_pt`, `fill_color_rgba`), `WhiteoutReport` (`covered_marks`, `text_characters_under_cover`, `content_removed` always `false`, `message`) and `PageRedactionOptions` (`page_indices`, `fill_color_rgba`, `overlay_text`) in `src/pdf/redactor.h`. Done when: the header compiles across `recto-pdf`, `recto-core` and `recto-cli`.
- [ ] Implement `Redactor::whiteout`: append an opaque `re f` in the chosen colour at the end of the page contents inside `/Artifact << /RectoWhiteout true >> BDC ... EMC`, count the text characters whose quads (`PageText`) lie under the cover, and write on a copy with `AtomicFileWriter`. Done when: `Whiteout.CoverKeepsContentAndSaysSo` renders the cover, still extracts the covered words, and reports the count and `content_removed = false`. Cheaper substitute: naming the cover a redaction or hiding the remaining-text count.
- [ ] Implement `Redactor::redact_pages`: replace each chosen page's `/Contents` with only the burn-in rectangle and optional overlay text, remove its `/Annots`, `/Thumb`, `/PieceInfo`, `/AA`, article beads and structure associations, and write through a full `PdfWriter` rewrite that keeps only objects reachable from the new document so the old content, fonts and images are gone. Done when: `RedactPages.WholePageRemoved` finds no byte of the page's former text, images or fonts in the raw output and unredacted pages render pixel-identical.
- [ ] Keep references honest: bookmarks, named destinations and page labels that name a redacted page are kept and listed in the report as `references_to_review` so the person can check titles that may reveal the removed content, never edited silently. Done when: `RedactPages.ReferencesListed` lists the bookmark and destination of a redacted page.
- [ ] Refuse the whole operation, writing nothing, when any chosen page cannot be parsed, as `D06 T03 §6` requires of apply. Done when: `RedactPages.UnreadableRefusesAll` yields an error and an unchanged source hash.
- [ ] Extend `recto-cli redact` with `--pages <spec>` (whole-page redaction, with `--label <text>`) and add `recto-cli whiteout <in.pdf> --out <out.pdf> --rect <page,x,y,w,h>... [--color <rrggbb>] [--json]`, which always prints `NOT REDACTION: content under the cover remains in the file` and exits `0` only after writing. Done when: `Cli.WhiteoutAndRedactPages` verifies the message, the JSON `"redaction": false` and the page-range redaction.
- [ ] Add the audit relationship test: `D06 T03 §6`'s cover audit flags Recto's whiteout output and reports zero findings on `redact_pages` output. Done when: `Whiteout.AuditFlagsCover` passes both directions.
- [ ] Add `Whiteout.*` and `RedactPages.*` tests to `tests/recto-tests/redactor_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Whiteout.*:RedactPages.*:Cli.WhiteoutAndRedactPages` passes under ASan.
- [ ] Commit: `organize: whiteout labelled as not redaction, and whole-page redaction`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Whiteout.*:RedactPages.*:Cli.WhiteoutAndRedactPages` passes under ASan with the "still extractable under a whiteout" and "nothing left after page redaction" results quoted.

## 25. PII country packs with checksum validation, local only (engine and CLI)

Foxit PDF Editor's and PDFelement's redaction find personal data by pattern; Recto's six regular-expression presets (`D05 T02 §9`) do not check that a number is plausible, so they redact false positives and miss formatted numbers. `PiiDetector` (`src/core/pii_detector.h/.cpp`) adds country packs that pair a pattern with the checksum or structure rule that country's identifier follows, runs only on the extracted page text (`PageText`, `D03 T01 §1`), and uses no network and no model. It returns matches with character quads so the redaction flow (`D05 T02 §9`) can mark them for review, never apply them. The shipped set is a default chosen for reach, and a pack is one registry entry, so adding a country does not touch the engine. Optional AI-assisted detection is a separate feature (`D08`) and not part of this section.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the pack registry, the shared matcher (word boundaries, separators, quads) and the `intl` pack (IBAN, payment cards, E.164 phone, passport MRZ). Done when: `PiiPacks.Intl` finds full IBAN, Luhn-valid card, E.164 and MRZ matches only, with no fragment of a match surviving.
2. Add the country packs and the confidence model, then the CLI. Done when: `PiiPacks.CountryPacks`, `PiiPacks.ContextAndConfidence`, `PiiPacks.BoundsAndSpeed` and `Cli.PiiScanCommand` pass under ASan.

- [ ] Define `PiiPack` (`id`, `display_name`, `detectors`), `PiiDetectorSpec` (`kind`, `pattern`, `validator`, `context_keywords`), `PiiValidation` (`ChecksumValid`, `StructureValid`, `FormatOnly`), `PiiConfidence` (`High`, `Low`) and `PiiMatch` (`pack_id`, `kind`, `page_index`, `quads`, `text`, `validation`, `confidence`) in `src/core/pii_detector.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement the shared matcher over `PageText`: candidates are found on the normalised text, separators (space, hyphen, dot) are tolerated inside groups, a match must not be part of a longer digit or letter run, the match is mapped back to character quads across glyph runs, and a candidate that spans a line break is rejected. Done when: `PiiPacks.Intl` finds a hyphen-grouped number split across two glyph runs with exact quads and rejects a number inside a longer digit string.
- [ ] Implement the `intl` pack: IBAN with the ISO 13616 country length table (`resources/pii/iban-lengths.csv` with its source recorded in `tests/corpus/PROVENANCE.md`) and the mod-97 check, payment cards `13` to `19` digits with the Luhn check and issuer prefix ranges, E.164 numbers (`+` and up to `15` digits) with plausible country calling codes, and ICAO 9303 machine-readable-zone lines whose check digits (weights `7, 3, 1`) validate. Done when: `PiiPacks.Intl` accepts valid synthetic values and rejects a one-digit change in each.
- [ ] Implement validators for the Luhn family (Canadian SIN, South African ID number with its date part, French SIREN and SIRET), the mod-11 family (UK NHS number, Dutch BSN 11-proof, Australian TFN with weights `1, 4, 3, 7, 5, 8, 6, 9, 10`, Brazilian CPF and CNPJ), the French NIR with its key `97 - (n mod 97)`, the Spanish DNI and NIE control letter, the Italian codice fiscale control character, the German tax identification number check, the Indian Aadhaar Verhoeff check and PAN structure, the US SSN and ITIN structure rules (no `000`, `666` or `9xx` area for SSN, group `00` or serial `0000`) and the UK National Insurance number format with unallocated prefixes refused. Done when: `PiiPacks.CountryPacks` has accepted and rejected cases for each validator built from synthetic numbers.
- [ ] Add the confidence model: a detector with a checksum yields `High`; a structure-only or format-only detector (US SSN, EIN, PAN, phone without `+`) yields `High` only when one of its localised context keywords (`SSN`, `tax id`, `NIF`, `passport` and the like) appears within `40` characters before or after, otherwise `Low`. Done when: `PiiPacks.ContextAndConfidence` shows the same number `High` with a keyword and `Low` without.
- [ ] Bound the work: scan at most `1 MiB` of text per page (the rest reported as `page_truncated`), reject any candidate regex that is not linear-time, honour `CancelToken` between pages, and keep every pack's regular expressions in a table checked by a test for catastrophic backtracking. Done when: `PiiPacks.BoundsAndSpeed` scans a `500`-page text fixture within the stated budget and a pathological page returns `page_truncated`.
- [ ] Implement `recto-cli pii scan <in.pdf> [--packs <ids>] [--include-low] [--json]` in `src/cli/cmd_pii.cpp`, listing matches with page, pack, validation and confidence and no raw document text beyond the match. Done when: `Cli.PiiScanCommand` verifies the pack selection, the JSON and the `Low` filter.
- [ ] Add `PiiPacks.*` tests with synthetic fixtures only (numbers computed from random bases at test time, never real people's identifiers) to `tests/recto-tests/pii_packs_tests.cpp` and record the policy in `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=PiiPacks.*:Cli.PiiScanCommand` passes under ASan.
- [ ] Commit: `organize: PII country packs with checksum validation, local only`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PiiPacks.*:Cli.PiiScanCommand` passes under ASan with the accepted and one-digit-changed rejected tables quoted.

## 26. Redaction profiles: saved patterns, appearance and scope as a JSON file (engine and CLI)

Foxit PDF Editor and PDFelement let a team save a redaction pattern set and reuse it. A `RedactionProfile` (`src/core/redaction_profile.h/.cpp`) is a small versioned JSON file naming patterns (a regular expression, a literal, or a PII pack from `§25`), the burn-in appearance, an optional page scope and optional sanitize categories. It never contains document text or earlier matches, so a profile can be shared safely. Profiles produce marks for review by default (`MarkOnly`); applying without review needs the explicit `--apply` flag on the CLI, and the desktop UI never applies without its confirmation (`D05 T02 §9`). The store writes atomically and parses hostile files under hard bounds.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the schema, the parser with bounds and the atomic store. Done when: `RedactionProfile.ParseAndStore` round-trips a profile and `RedactionProfile.HostileFiles` refuses the oversize, deep, unknown-version and catastrophic-pattern files.
2. Resolve a profile against a document into marks and add the CLI. Done when: `RedactionProfile.ResolveMarks` and `Cli.RedactProfileCommand` pass under ASan and `fuzz_redaction_profile` (60 second smoke) is clean.

- [ ] Define `RedactionProfile` (`schema_version` `1`, `name`, `description`, `patterns` of `kind` `Regex`, `Literal` or `PiiPack` with `value`, `case_sensitive`, `whole_word`, `appearance` of `fill_color`, `overlay_text`, `text_color`, `page_scope`, `sanitize` of `SanitizeOptions` flags, `apply_mode` of `MarkOnly` or `ApplyDirect`) in `src/core/redaction_profile.h`, with a JSON schema in `resources/schemas/redaction-profile.schema.json`. Done when: the header compiles and `RedactionProfile.SchemaMatchesParser` validates example files against the schema.
- [ ] Implement `parse_redaction_profile(bytes)` with a `256 KiB` file limit, at most `200` patterns, a `2 KiB` limit per pattern, depth at most `8`, an unknown `schema_version` refused naming it, unknown keys ignored with a warning, and every regular expression compiled with the find engine's linear-time rule and a per-page step budget, refusing a catastrophic pattern such as `(a+)+$` naming it. Done when: `RedactionProfile.HostileFiles` covers each refusal.
- [ ] Implement `RedactionProfileStore` in `AppPaths::data_dir()/redaction-profiles/` with one JSON file per profile written atomically (temp-then-replace), list, rename, delete, import and export of a single file, name collisions resolved by suffix, and the shipped read-only profiles generated from the packs of `§25`. Done when: `RedactionProfile.ParseAndStore` round-trips, `RedactionProfile.AtomicWriteCrash` leaves the old file on a simulated mid-write failure, and a read-only profile cannot be edited.
- [ ] Implement `resolve_profile(doc, profile, CancelToken)` returning `RedactionMark`s (`D05 T01 §10`) from regex and literal matches over `PageText`, from `PiiDetector` for pack patterns (`§25`), and from the page scope, with each mark carrying the pattern that produced it. Done when: `RedactionProfile.ResolveMarks` returns the expected quads for a fixture with an email, an IBAN and a literal.
- [ ] Extend `recto-cli redact` with `--profile <file.json>`, `--marks-out <marks.json>` (writing the pending marks without changing the PDF), `--marks <marks.json>` (reading marks produced earlier) and `--apply`, so the default invocation with a profile only writes marks and applying always needs `--apply --out`. Done when: `Cli.RedactProfileCommand` verifies that omitting `--apply` writes no PDF and that `--apply` removes the matched text from the raw bytes.
- [ ] Add a libFuzzer target `fuzz_redaction_profile` in `fuzz/fuzz_redaction_profile.cpp`. Done when: a 60 second smoke run (`D01 T03 §5` CI mode) finds no crash, hang or unbounded allocation.
- [ ] Add `RedactionProfile.*` tests to `tests/recto-tests/redaction_profile_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=RedactionProfile.*:Cli.RedactProfileCommand` passes under ASan.
- [ ] Commit: `organize: redaction profiles as versioned JSON with marks-only default`

**Test checkpoint:** `recto-tests.exe --gtest_filter=RedactionProfile.*:Cli.RedactProfileCommand` passes under ASan with the catastrophic-pattern refusal and the no-apply-without-flag result quoted.

## 27. Document inspector report: what a PDF carries before sanitizing (engine and CLI)

Acrobat Pro's Inspect Document and Foxit PDF Editor's equivalent list what a file hides (scripts, attachments, hidden layers, metadata, comments, form data, hidden text, earlier revisions) so a person can decide what to remove. `inspect_document` (`src/pdf/inspector.h/.cpp`, `recto::pdf::Inspector`) builds that report from the document without executing anything: JavaScript is listed and shown as bounded escaped text, never run (D9); links and attachments are listed, never opened. Each item names the `SanitizeOptions` flag of `§10` that removes it, so the report and the sanitizer agree, and the same counts feed the Sanitize dialog. Hidden text includes text under a later opaque shape, the usual sign of a cover that was not a redaction.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the structural categories (JavaScript and actions, embedded files, layers, metadata, comments, form data, revisions, thumbnails). Done when: `Inspector.StructuralCategories` reports the expected items for a fixture that contains each.
2. Implement hidden-text detection over the `DisplayList`, the report model and the CLI. Done when: `Inspector.HiddenText`, `Inspector.BoundsAndTruncation` and `Cli.InspectCommand` pass under ASan.

- [ ] Define `InspectionCategory` (`JavaScript`, `Actions`, `EmbeddedFiles`, `HiddenLayers`, `Metadata`, `Comments`, `FormData`, `HiddenText`, `PreviousRevisions`, `Thumbnails`, `ExternalLinks`), `InspectionItem` (`category`, `location` as page or object reference, `summary`, `detail` bounded to `4 KiB` and escaped, `size_bytes`, `risk` of `Info`, `Review` or `Warning`, `removable_by` as a `SanitizeOptions` flag name or `none`) and `InspectionReport` (`items`, `counts_by_category`, `truncated`) in `src/pdf/inspector.h`. Done when: the header compiles across `recto-pdf`, `recto-core` and `recto-cli`.
- [ ] List JavaScript from `/Names /JavaScript`, `/OpenAction`, document, page, field and annotation `/AA` entries and `/JS` actions, and other actions (`Launch`, `URI`, `SubmitForm`, `ImportData`, `GoToR`, `GoToE`) with their targets, showing script text escaped and truncated and never evaluating it. Done when: `Inspector.StructuralCategories` lists a `Launch` action and a script, and `Inspector.NeverExecutes` shows the script's side effect did not occur.
- [ ] List embedded files from `/Names /EmbeddedFiles` and file-attachment annotations with name, size, declared type and SHA-256, hidden layers (optional content groups whose default state is off or that are referenced by `/OC`), metadata (`/Info` keys, XMP presence and size, `/PieceInfo`), comment authors and counts by subtype, form fields that hold values (and an `/XFA` entry), thumbnails, and external link counts. Done when: `Inspector.StructuralCategories` matches the fixture's known contents.
- [ ] Count previous revisions by walking the `xref` and `/Prev` chain and `%%EOF` markers (`D01 T01 §5`), reporting each earlier revision's size and the objects it replaced, mapped to the `Deleted objects and incremental history` sanitize flag. Done when: `Inspector.PreviousRevisions` reports `3` revisions for a fixture with two incremental updates.
- [ ] Detect hidden text from the interpreter's `DisplayList`: glyph runs in render mode `3` (marked `likely OCR layer` when the page also has a page-sized image), runs fully outside the page's `/CropBox`, runs with an effective size under `1 pt`, and runs whose quads are fully covered by a later opaque fill (marked `possible cover without redaction`). Done when: `Inspector.HiddenText` flags each planted kind and does not flag ordinary visible text or an OCR layer without the covering image.
- [ ] Bound the work: at most `10000` items (setting `truncated`), recursion depth at most `32` in action and name trees, cycle-safe, and `CancelToken` honoured between pages. Done when: `Inspector.BoundsAndTruncation` inspects a hostile tree fixture and a `20000`-annotation fixture without crash or unbounded allocation.
- [ ] Implement `recto-cli inspect <in.pdf> [--json] [--category <name>...]` in `src/cli/cmd_inspect.cpp`, printing a summary table or the JSON report and exiting `0` whatever it finds. Done when: `Cli.InspectCommand` verifies the JSON fields and category filter.
- [ ] Add `Inspector.*` tests and the fixture (script, launch action, attachment, hidden layer, XMP, comment, form value, two incremental updates, hidden text kinds) to `tests/recto-tests/inspector_tests.cpp`, `scripts/build-corpus.py` and `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=Inspector.*:Cli.InspectCommand` passes under ASan.
- [ ] Commit: `pdf: document inspector report for hidden content, never executing it`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Inspector.*:Cli.InspectCommand` passes under ASan with the per-category counts for the fixture quoted.

## 28. Signed revision extraction and comparison with the current document (engine and CLI)

Foxit PDF Editor and Acrobat Pro can show the version of a document as it was when a signature was applied and what changed since. `SignedRevision` (`src/pdf/signed_revision.h/.cpp`) takes a verified signature (`§9`), validates its `/ByteRange` with the same hostile-input checks as the verifier, and writes the exact bytes `[0, off2 + len2)` that the signer covered to a read-only temp file: a byte-for-byte prefix of the file, never a re-save, so its own signature still verifies. `compare_signed_with_current` then runs the document comparison engine (`D03 T01 §10`) between that revision and the current file and classifies the differences with the verifier's incremental-update analysis, so allowed changes (form filling, counter-signatures) and unauthorized edits are told apart.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `extract_signed_revision` with validation and the temp file. Done when: `SignedRevision.ExtractPrefix` writes a prefix whose bytes equal the source's first `off2 + len2` bytes and which `verify_signatures` reports `ValidTrusted` or `ValidUntrustedRoot` on its own.
2. Implement the comparison report and the CLI. Done when: `SignedRevision.CompareWithCurrent`, `SignedRevision.Hostile` and `Cli.SignedRevisionCommand` pass under ASan.

- [ ] Define `SignedRevisionResult` (`signature_field_name`, `revision_index`, `revision_bytes`, `covers_entire_file`, `temp_path`) and `SignedVsCurrentReport` (`pages_added`, `pages_removed`, `pages_changed` with text and pixel deltas, `annotations_added`, `annotations_removed`, `annotations_modified`, `form_fields_changed`, `metadata_changed`, `later_signatures`, `classification` of `NoChanges`, `AllowedChangesOnly` or `UnauthorizedChanges`) in `src/pdf/signed_revision.h`. Done when: the header compiles across `recto-pdf`, `recto-core` and `recto-cli`.
- [ ] Implement `extract_signed_revision(src_path, signature_selector, dst_path)` reusing the strict `/ByteRange` validation of `§9` (`b0 == 0`, no gap outside `/Contents`, `b1 + l1 <= file_size`), requiring the prefix to end at an `%%EOF` marker followed only by line endings, and returning `SignedRevisionIncomplete` otherwise. Done when: `SignedRevision.ExtractPrefix` passes and `SignedRevision.Hostile` refuses a range that skips a stream and a prefix with no `%%EOF`.
- [ ] Write the prefix byte for byte through `AtomicFileWriter` to `AppPaths::temp_dir()/signed-revisions/` as a read-only file, never altering the source, and delete it on close and at next launch. Done when: `SignedRevision.TempLifecycle` shows the file created read-only, the source hash unchanged and the file removed on close.
- [ ] Implement `compare_signed_with_current(src_path, signature_selector, CancelToken)` over `compare_documents` (`D03 T01 §10`) for page text and pixels, the annotation and field trees, and `/Info`, and fill `classification` from the incremental-update analysis of `§9` (counter-signatures and form fills allowed by `/DocMDP`, page content or structure changes unauthorized). Done when: `SignedRevision.CompareWithCurrent` reports an added annotation after signing as `UnauthorizedChanges` under `P=1` and a filled field as `AllowedChangesOnly` under `P=2`.
- [ ] Handle a file with several signatures: `signature_selector` takes a field name or an index in revision order, the report lists `later_signatures`, and a signature that covers the entire file reports `covers_entire_file = true` with `NoChanges`. Done when: `SignedRevision.MultipleSignatures` covers a counter-signed file.
- [ ] Implement `recto-cli signed-revision <in.pdf> --signature <name|index> --out <revision.pdf>` and `recto-cli signed-revision <in.pdf> --signature <name|index> --compare [--json]` in `src/cli/cmd_signature.cpp`. Done when: `Cli.SignedRevisionCommand` verifies the extraction, the comparison JSON and the refusals.
- [ ] Add `SignedRevision.*` tests to `tests/recto-tests/signed_revision_tests.cpp` using certificates created at test time. Done when: `recto-tests.exe --gtest_filter=SignedRevision.*:Cli.SignedRevisionCommand` passes under ASan.
- [ ] Commit: `pdf: extract the signed revision and compare it with the current document`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SignedRevision.*:Cli.SignedRevisionCommand` passes under ASan with the byte-equality of the prefix and the two classifications quoted.

## 29. Trust lists: opt-in download and manual import into a Recto trust store (engine and CLI)

Foxit PDF Editor trusts signers through the Adobe Approved Trust List and the European Union trusted lists; Recto's verifier (`§9`) otherwise relies on the Windows certificate store alone, so a signature from a European qualified service provider may read as `ValidUntrustedRoot` on a machine that does not already trust it. `TrustStore` (`src/pdf/trust_store.h/.cpp`) is a Recto-only set of extra trust anchors, never added to the Windows store. A list reaches it by manual import (always available) or by an opt-in download (off by default, D16: nothing is fetched until the person turns it on and asks). Import accepts certificate bundles (`.cer`, `.crt`, `.pem`, `.p7b`) and an ETSI TS 119 612 trusted-list XML file. Known limit, stated in the UI: this version does not verify the XML signature on a downloaded list, so each list needs the person's confirmation of its SHA-256 fingerprint and source, and the report says "signature not verified". The verifier uses the store only after the Windows chain is untrusted, through a chain engine whose exclusive root store is the Recto store.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the store, the bundle and trusted-list parsers with hostile-input bounds, and the verifier hook. Done when: `TrustStore.ImportBundle`, `TrustStore.ParseTrustedList` and `TrustStore.VerifierUsesStore` pass under ASan and `fuzz_trust_list` (60 second smoke) is clean.
2. Implement the opt-in downloader and the CLI. Done when: `TrustStore.DownloadOptIn`, `TrustStore.DownloadBounds` and `Cli.TrustCommand` pass.

- [ ] Define `TrustListRecord` (`id`, `name`, `kind` of `Manual`, `CertificateBundle` or `EuTrustedList`, `source_url`, `imported_utc`, `issue_date`, `next_update`, `sha256`, `certificate_count`, `enabled`, `signature_verified` always `false` in this version, `confirmed_by_user`) and the `TrustStore` interface in `src/pdf/trust_store.h`. Done when: the header compiles across `recto-pdf`, `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement the store under `AppPaths::data_dir()/trust/` as a JSON index plus one DER certificate file per list, written atomically (temp-then-replace), with enable, disable, remove and list, and no copy of any certificate into the Windows store. Done when: `TrustStore.AtomicAndIsolated` shows a simulated mid-write failure keeping the old index and the Windows `ROOT` store unchanged after import.
- [ ] Implement certificate-bundle import for `.cer`, `.crt` (DER or PEM), `.pem` and `.p7b`, bounded to `5 MiB` per file and `5000` certificates, skipping expired certificates with a count, and recording the list as `confirmed_by_user` only after the caller passes the confirmed fingerprint. Done when: `TrustStore.ImportBundle` imports each format and refuses an oversize file and a fingerprint mismatch.
- [ ] Implement the ETSI TS 119 612 trusted-list parser with a hardened XML reader (no DTD, no external entities, depth at most `64`, `20 MiB` limit, linear time) that extracts, per trust service provider, the certificates of services of the qualified certification-authority type whose current status is granted, plus the list's issue date and next update. Done when: `TrustStore.ParseTrustedList` extracts the expected certificates from a synthetic list and rejects a billion-laughs and an external-entity file.
- [ ] Hook the verifier (`§9`): when `CertGetCertificateChain` against the Windows default engine is untrusted, rebuild the chain with `CertCreateCertificateChainEngine` whose `hExclusiveRoot` is the enabled Recto store, and report `ValidTrusted` with `trust_source = "Recto trust list: <name>"` only when that chain is valid and the policy check passes, otherwise leave the Windows result. Done when: `TrustStore.VerifierUsesStore` shows an untrusted-root fixture becoming trusted by an imported list and returning to untrusted when the list is disabled.
- [ ] Implement `TrustListDownloader` through WinHTTP: HTTPS only, certificate validation on, redirects only to HTTPS, no cookies or credentials, a `20 MiB` limit per file and `60 s` timeout, a User-Agent with the app version and nothing else, never started unless the setting `trust.downloadEnabled` is on and the person pressed `Update now` (or the optional once-a-week check they enabled), and a list reference followed only to HTTPS URLs found in the downloaded index with at most `64` lists per update. Done when: `TrustStore.DownloadOptIn` shows no network call with the setting off and `TrustStore.DownloadBounds` refuses an HTTP redirect, an oversize body and a `65`th list, against a local test server.
- [ ] Ship no default AATL address and one editable default for the EU list of lists (`https://ec.europa.eu/tools/lotl/eu-lotl.xml`, recorded as a default whose change cost is one settings value), and show in every result that the list's signature was not verified. Done when: `TrustStore.NoDefaultAatl` finds the setting empty and `TrustStore.UnverifiedNotice` finds the notice in the result.
- [ ] Add a libFuzzer target `fuzz_trust_list` in `fuzz/fuzz_trust_list.cpp` mutating trusted-list XML and PEM bundles. Done when: a 60 second smoke run (`D01 T03 §5` CI mode) finds no crash, hang or unbounded allocation.
- [ ] Implement `recto-cli trust list [--json]`, `recto-cli trust import <file> --confirm-sha256 <hex> [--name <text>]`, `recto-cli trust remove <id>`, `recto-cli trust enable|disable <id>` and `recto-cli trust update [--url <https-url>]` in `src/cli/cmd_trust.cpp`, where `update` refuses to run while `trust.downloadEnabled` is off. Done when: `Cli.TrustCommand` verifies each verb and the opt-in refusal.
- [ ] Add `TrustStore.*` tests with test-time certificates and synthetic trusted lists to `tests/recto-tests/trust_store_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=TrustStore.*:Cli.TrustCommand` passes under ASan.
- [ ] Commit: `pdf: Recto trust store with manual import and opt-in trusted-list download`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TrustStore.*:Cli.TrustCommand` passes under ASan with the no-network-when-off and untrusted-to-trusted results quoted.

## 30. PDF Binder: combine files with a table-of-contents page and a project file

Foxit PDF Editor's PDF Binder combines files into one document with a generated table of contents and keeps a project so the binder can be rebuilt when a source changes; Acrobat's Combine does not. `build_binder` (`src/core/binder.h/.cpp`) reads a `.rectobinder` JSON project (versioned, bounded, never storing passwords), merges the listed files with `merge_pdfs` (`§2`), prepends a generated table-of-contents page set with a clickable entry per item and a page number per entry, adds a bookmark per item, and labels the table pages `i, ii, ...` and the body from `1` using `§19`. The project records each input's SHA-256 and size at the last build, so `binder check` tells which sources changed or went missing and `binder rebuild` regenerates the output as a new file. Inputs are never modified or overwritten.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the project schema, the parser and the table-of-contents layout with a two-pass page count. Done when: `Binder.ProjectParse` and `Binder.TocLayout` pass, including a table that spills onto a second page.
2. Build, check and rebuild, and add the CLI. Done when: `Binder.BuildAndRebuild`, `Binder.ChangedAndMissingInputs`, `Binder.HostileProject` and `Cli.BinderCommand` pass under ASan and `fuzz_binder_project` (60 second smoke) is clean.

- [ ] Define `BinderProject` (`schema_version` `1`, `title`, `items` of `path`, `page_selection`, `title_override`, `include_in_toc`, `toc` of `enabled`, `title`, `font_family`, `font_size_pt`, `leader_dots`, `depth`, `output` of `path_template`, `last_build` of per-item `sha256`, `size`, `page_count` and `built_utc`) in `src/core/binder.h` with a schema in `resources/schemas/binder-project.schema.json`; paths are stored relative to the project file when possible. Done when: the header compiles and `Binder.ProjectParse` validates example projects against the schema.
- [ ] Implement `parse_binder_project(bytes)` with a `1 MiB` limit, at most `1000` items, depth at most `8`, an unknown `schema_version` refused naming it, and a typed fault for a path that escapes to a UNC or device path (`\\.\`, `\\?\`) the caller did not allow. Done when: `Binder.HostileProject` covers each refusal.
- [ ] Lay out the table of contents with the stamp text emitter of `§6` (Unicode text with `/ToUnicode`, DirectWrite metrics): one line per item and per bookmark down to `depth`, right-aligned page numbers with optional dot leaders, wrapping long titles, and a two-pass count so the page numbers account for the table's own length. Done when: `Binder.TocLayout` shows correct page numbers for a table that fits one page and one that spills to two, and `SearchEngine` finds an entry title.
- [ ] Implement `build_binder(project, passwords, dst, CancelToken)`: merge the items with `merge_pdfs`, prepend the table pages, make every entry a `/Link` annotation with a `/GoTo` destination to its item's first page, add one top-level bookmark per item (with the item's own bookmarks beneath) and a `Table of Contents` bookmark, and label the table pages lowercase Roman and the body decimal from `1` through `set_page_labels` (`§19`). Done when: `Binder.BuildAndRebuild` builds a three-file binder whose entries link to the right pages and whose labels read `i` then `1`. Cheaper substitute: a flat merge with a text list that has no links, bookmarks or page labels.
- [ ] Record each input's SHA-256, size and page count in `last_build` after a successful build, written to the project file atomically, and implement `check_binder(project)` listing items that are `Unchanged`, `Changed`, `Missing` or `New` since the last build. Done when: `Binder.ChangedAndMissingInputs` reports each state after editing, deleting and adding a source.
- [ ] Rebuild to a new file name from the template (never replacing an existing file or any input), take passwords from the caller and never store them in the project, and surface `PasswordRequired` naming the item. Done when: `Binder.BuildAndRebuild` shows the previous output and all inputs byte-identical after a rebuild and `Binder.EncryptedInput` names the encrypted item.
- [ ] Add a libFuzzer target `fuzz_binder_project` in `fuzz/fuzz_binder_project.cpp`. Done when: a 60 second smoke run (`D01 T03 §5` CI mode) finds no crash, hang or unbounded allocation.
- [ ] Implement `recto-cli binder build <project.rectobinder> [--out <out.pdf>] [--password <file>=<pw>...]`, `recto-cli binder check <project.rectobinder> [--json]` and `recto-cli binder init <out.rectobinder> <file.pdf>...` in `src/cli/cmd_binder.cpp`. Done when: `Cli.BinderCommand` verifies each verb and the exit codes.
- [ ] Add `Binder.*` tests to `tests/recto-tests/binder_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Binder.*:Cli.BinderCommand` passes under ASan.
- [ ] Commit: `organize: PDF Binder with a linked table of contents and a rebuildable project`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Binder.*:Cli.BinderCommand` passes under ASan with the linked-entry page check and the changed-source report quoted.

## Verification

- M4's merge, split, reorder, rotate, delete, blank-page insert, crop/resize, watermarks, headers/footers, Bates numbering, encrypt/decrypt, digital signatures, and true redaction/sanitization each pass under ASan and round-trip in `D06 T01 §6`'s suite.
- No operation path modifies the open viewer's document instance in place.
- Every page tool of `§15` to `§23` and `§30` leaves its source byte-identical on success, failure and cancel, and each has a `recto-cli` verb.
- Nothing in `§24` to `§29` executes document content or touches the network unless the person opted in (`trust.downloadEnabled`).
- `python scripts/todo-graph.py validate` passes.
