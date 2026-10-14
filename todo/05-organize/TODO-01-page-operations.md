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

> **Current state:** Measured 2026-10-14. No code exists. The mechanisms are plan section 5's rows: `recto::pdf::PdfWriter` with one `recto::pdf::GraftMap` per source for merge and split, page-tree functions on a copy for reorder, rotate, delete, blank-page insertion and box cropping, `src/core/stamp_watermark.cpp` for watermarks, headers/footers and Bates numbering, `src/pdf/crypto.cpp` for `AES-256` (`R=6`) encryption, `src/pdf/signature.cpp` for PKCS#7 / PAdES digital signatures, and `src/pdf/redactor.cpp` for physical redaction and sanitization, all saved through the atomic rule.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D9 and D12; sections 4.1 (`graft.cpp`, `crypto.cpp`, `signature.cpp`, `redactor.cpp`), 4.9 (job isolation), 5 (Merge, Split, Rotate/delete/reorder/crop, Watermarks/headers/footers/Bates, Encrypt/decrypt, Digital Signatures, True Redaction & Sanitization), and 9 (security rules).
- `design/SPEC.md` sections 8.2 (`Redaction & Sanitize Bar`, `Signature Status Banner & Sign Mode`), 8.3 (`Signatures & Standards` side panel), 9.5 (`Protected & Signed PDF` and `Unlocked PDF` cards), 9.6 (`Organize` tools), and 9.9 (`Watermark`, `Header/Footer & Bates`, `Crop & Resize`, `Sanitize Document`, and `Sign Document` dialogs).
- -> XREF: D04 T02 §1 -- the shared writer plumbing and atomic save helper this file consumes; graft-map needs (per-source maps) feed back into that plumbing's shape.
- -> XREF: D05 T02 §1 -- the Organize grid, dialogs, and viewer security bars whose edits compile down to this file's operation list; the operation vocabulary is agreed here.
- -> XREF: D06 T02 §1 -- the security audit in `D06 T02 §1` verifies physical glyph/pixel removal after redaction and metadata/hidden-data eradication after sanitization.

## Outcome

- Merging N PDFs yields one PDF with pages in the stated order, byte-level page content preserved by grafting (no re-render), verifiable page by page.
- Splitting by ranges, every N, or per page yields exactly the stated files; reorder, rotate, delete and extract round-trip through render comparison.
- Encryption produces AES-256 (`R=6`) files with the stated permissions that other readers honor; decryption with the right password produces a clean unprotected copy, and the wrong password is a typed refusal.
- Blank-page insertion, `/MediaBox`/`/CropBox`/`/TrimBox`/`/BleedBox`/`/ArtBox` cropping and page resizing, vector/image watermarks, headers/footers, and Bates numbering (`src/core/stamp_watermark.cpp`) produce clean vector PDF streams and CLI operations.
- `src/pdf/signature.cpp` signs and verifies PDFs using PKCS#12 (`.pfx`/`.p12`) and the Windows Certificate Store (`crypt32.dll` / `bcrypt.dll`) with PKCS#7 detached and PAdES (`B-B`, `B-T`, `B-LT`), `/DSS` revocation data, RFC 3161 timestamps, `/DocMDP` certification, and incremental-save multi-signature support.
- `src/pdf/redactor.cpp` permanently removes text runs, vector paths, and image pixels inside redaction rectangles (never merely drawing a black box over selectable text) and sanitizes hidden metadata, scripts, attachments, and layers.

**Adjacency:** all=not-applicable (engine operations inside the D04 T01 job frame; cards, queue, notices and save settings are owned there and by D02 T04 §2)

**Adjacency rationale:** This file is the verb library the Convert cards, Organize UI, and Viewer security bars (`D05 T02`) call. Its surfaces live in `D04 T01` and `D05 T02`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Graft merge and split | D04 T02 §1 | [ ] |
| 2 | §2 | Reorder, rotate, delete and extract on a copy | §1 | [ ] |
| 3 | §3 | Encrypt and decrypt (AES-256) | §1 | [ ] |
| 4 | §4 | Insert blank page, crop/resize boxes, watermarks, headers/footers and Bates numbering | §2 | [ ] |
| 5 | §5 | Digital signatures (PKCS#7, PAdES, RFC 3161) and true physical redaction & sanitization | §3, D01 T02 §4 | [ ] |

## 1. Graft merge and split

Merge by grafting mapped pages from each source into a fresh `PdfWriter` document (object-level deep copy, shared resources such as fonts, ICC profiles, and images de-duplicated per source by `GraftMap`), preserving each page's content and rotation; split is the same machinery into several documents by range spec (explicit ranges, every N, one per page). Source outlines become top-level entries naming the source file where the stretch goal is taken for v1 (decide and record in this section when building; the Decide is cheap here and expensive in the UI). Encrypted sources flow through the job's password recovery (D04 T01 §1), never an engine-level prompt.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/pdf/graft.cpp` (`GraftMap` object-graph cloning and resource deduplication) and `src/core/page_ops.cpp` merge and split operations over a source list with per-source page selections. Done when: merging three corpus files and splitting the result back by the original ranges reproduces pages that pixel-match the originals through `recto-cli render`.
2. Range-spec parsing in core (one parser the UI and cards share, with the same refusal for out-of-range pages). Done when: invalid specs refuse before any file is written, in tests.

- [ ] Merge and split through graft maps. Done when: a merged file's pages render identical to sources at two zooms. Cheaper substitute: rendering pages to images and reassembling, destroying text and vector content (refused by plan section 5's mechanism choice).
- [ ] Shared range parser with pre-write refusal. Done when: `3-2`, `0`, and beyond-end ranges each refuse naming the fault.
- [ ] Commit: `organize: graft-based merge and split with shared range parsing`

**Test checkpoint:** `recto-tests --gtest_filter=PageOps.*` passes under ASan: merge-split round-trip pixel equality and range refusals (quote files and pages).

## 2. Reorder, rotate, delete and extract on a copy

The Organize edit vocabulary as one operation list applied to a copy of the source: a permutation (new order with deletions absent), per-page rotation deltas, and extraction as split-to-one-file reusing §1. Applying a list produces one output document; the source file and any open viewer document of it are untouched (the UI's save semantics, including replace-original, live in D05 T02 §4 over the atomic helper).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the operation list in `src/core/page_ops.cpp` over §1's graft machinery (a permutation is a single-source graft in a new order; rotation writes the page `/Rotate` delta through `PdfWriter`). Done when: a scripted edit (move page 3 to front, rotate page 2 by 90, delete page 5) renders exactly as specified against reference renders.
2. Extract as its own named operation for the UI's "Extract to new PDF". Done when: extracting a middle range equals splitting to that range, byte-comparable renders.

- [ ] Operation list application on a copy. Done when: applying an empty list reproduces the source's renders page for page. Cheaper substitute: mutating the loaded document and saving it, which plan section 5 forbids for the viewer document.
- [ ] Rotation persisted as page rotation, not re-rendered content. Done when: the output's page boxes report the rotated dimensions through `recto-cli info`.
- [ ] Commit: `organize: reorder, rotate, delete and extract as copy operations`

**Test checkpoint:** `recto-tests --gtest_filter=PageOps.*` green under ASan with the scripted-edit render comparisons quoted.

## 3. Encrypt and decrypt (AES-256)

Encrypt: save a copy with `PdfWriteOptions` using Windows CNG (`bcrypt.dll`) `AES-256` (`V=5, R=6`, ISO 32000-2 section 7.6.4), user and owner passwords, `/Perms` verification entry, and the permission set the UI chose (print, copy, edit, annotate), offered as the Protected PDF card. Decrypt: open with the supplied password and save an unprotected copy (the Unlocked card). Both follow the atomic rule and the SPEC 9.9 Replace original flow when the target is the source; the viewer document is never the thing being saved (plan section 5's note).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Engine encryption writer (`PdfWriteOptions` in `src/pdf/crypto.cpp` and `src/pdf/writer.cpp`) and core encrypt and decrypt operations with typed password outcomes through the job contract. Done when: the Permissions-and-security readback (D02 T03 §3's engine addition) reports AES-256 and exactly the permissions chosen, and a stock reader enforces a denied print permission on the output.
2. Decrypt round-trip: encrypt a corpus file, decrypt the result, renders equal the original. Done when: wrong-password decrypt is the typed refusal, never a partial file.

- [ ] AES-256 encrypt with permissions. Done when: the Info panel's security summary (D02 T03 §3) reads back method and permissions accurately. Cheaper substitute: the legacy 128-bit RC4 default a careless options struct gives.
- [ ] Decrypt with typed password outcomes. Done when: the queue's Enter password action (D04 T01 §3) completes a decrypt end to end.
- [ ] Commit: `organize: AES-256 encrypt and decrypt with permissions`

**Test checkpoint:** `recto-tests --gtest_filter=PageOps.*` green under ASan: encrypt-decrypt render equality, permission readback, wrong-password refusal (quoted).

## 4. Insert blank page, crop/resize boxes, watermarks, headers/footers and Bates numbering

Implement `src/core/stamp_watermark.cpp` and extend `src/core/page_ops.cpp`:
- **Insert Blank Page & Crop / Resize Page Boxes:** Insert blank pages at any index with configurable dimensions (`Match current page`, `A4`, `Letter`, `Legal`, `Custom pt/mm/in`) and orientation; update `/MediaBox`, `/CropBox`, `/TrimBox`, `/BleedBox`, and `/ArtBox` (with `Auto-Detect White Margins` via `DisplayList` bounding box inspection) or rescale page content streams (`q sx 0 0 sy tx ty cm ... Q`) to a new target paper size.
- **Watermarks & Stamps (`src/core/stamp_watermark.cpp`):** Inject reusable Form XObjects (`/Subtype /Form`) behind (prepend to `/Contents`) or in front of (append to `/Contents`) page streams with `/ExtGState` opacity (`/ca`, `/CA`), rotation angle (`-90` to `+90` degrees), 9-point anchor grid or tiled pattern, page-range and even/odd filters, supporting both formatted text watermarks and image/PDF vector logo watermarks.
- **Headers, Footers & Legal Bates Numbering:** Render left, center, and right header/footer zones with dynamic tokens (`{page}`, `{total}`, `{date}`, `{filename}`, `{author}`) and legal **Bates Numbering** (configurable prefix, suffix, zero-padded digit width e.g. `000001`, start number, multi-document sequential numbering across batches, and optional `Shrink page content to avoid overlapping existing text`).
- Expose `recto-cli watermark`, `recto-cli bates`, and `recto-cli crop`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement blank-page insertion, page-box cropping (`Auto-Detect White Margins`), and page content rescaling in `src/core/page_ops.cpp`, plus `recto-cli crop`. Done when: `PageOps.CropAndResize` inserts blank pages, crops margins, and rescales an A4 page to Letter with exact bounding box and visual verification under ASan.
2. Implement text/image watermarks, 3-zone headers/footers, and Bates numbering (including shrink-to-avoid-overlap) in `src/core/stamp_watermark.cpp`, plus `recto-cli watermark` and `recto-cli bates`. Done when: `StampWatermark.*` stamps a multi-page PDF with watermarks and `CASE-000101`..`CASE-000110` Bates numbers and verifies that `PageText` extracts the exact stamped Bates strings on each page.

- [ ] Blank-page insertion, `/MediaBox`/`/CropBox`/`/TrimBox`/`/BleedBox`/`/ArtBox` cropping, auto-margin detection, and page rescaling. Done when: `PageOps.CropAndResize` passes under ASan. Cheaper substitute: modifying `/CropBox` only when the user requested full page rescaling.
- [ ] Watermarks (text/image, under/over, opacity, rotation, tiling), 3-zone headers/footers, and Bates numbering (`src/core/stamp_watermark.cpp`). Done when: `StampWatermark.*` passes under ASan.
- [ ] Commit: `organize: add blank page insertion, crop/resize, watermarks, headers/footers and Bates numbering`

**Test checkpoint:** `recto-tests --gtest_filter=PageOps.CropAndResize:StampWatermark.*` passes under ASan.

## 5. Digital signatures (PKCS#7, PAdES, RFC 3161) and true physical redaction & sanitization

Implement `src/pdf/signature.cpp` (`recto::pdf::SignatureEngine`) and `src/pdf/redactor.cpp` (`recto::pdf::Redactor`):
- **Digital Signatures (`src/pdf/signature.cpp`):**
  - *Signing:* Load X.509 certificates and private keys from PKCS#12 (`.pfx` / `.p12`) files or the Windows Certificate Store (`CertOpenSystemStoreW(..., L"MY")`, supporting hardware tokens/smartcards via NCrypt) using Windows CryptoAPI / CNG (`crypt32.dll`, `bcrypt.dll`, `ncrypt.dll`). Write a `/FT /Sig` signature field and `/Type /Sig` dictionary with `/ByteRange [0 offset1 offset2 len2]` covering the entire file except the hex-encoded `/Contents` placeholder, compute SHA-256 / SHA-384 / SHA-512 CMS signed attributes (`CryptSignMessage` / `CMSG_SIGNED`), support both `/adbe.pkcs7.detached` and **PAdES** (`/ETSI.CAdES.detached`, `B-B`, `B-T`, `B-LT`, `B-LTA`), attach RFC 3161 Time-Stamp Authority (`TSA`) tokens and `/DSS` (Document Security Store) OCSP/CRL revocation responses, support `/DocMDP` certification permissions (`P=1, 2, 3`), synthesize visible signature `/AP` appearances (drawn ink, uploaded image, or typed signature + signer details), and write via **incremental save** (`D01 T02 §4`) so existing signatures remain valid when adding a counter-signature.
  - *Verification:* Verify `/ByteRange` digest integrity against file bytes, validate the CMS/CAdES cryptographic signature, build the X.509 certificate chain (`CertGetCertificateChain`), check revocation status and embedded `/DSS` / RFC 3161 timestamps, and detect any unauthorized incremental modifications after signing.
- **True Physical Redaction & Document Sanitization (`src/pdf/redactor.cpp`):**
  - *Physical Redaction:* Given a list of page redaction rectangles (from manual selection, text search, or regex patterns in `D03 T01 §3`), rewrite each affected page's `/Contents` stream and Form XObjects: split any `Tj`/`TJ` text string whose glyph quads intersect a redaction box and delete the intersecting characters completely from the content stream; clip or remove intersecting vector path segments; overwrite intersecting pixel regions in embedded `/Subtype /Image` XObjects with solid fill; remove matching entries from `/Annots`, `/StructTreeRoot`, and `/Outlines`; and paint the burn-in overlay rectangle with optional exemption code text (e.g., `FOIA (b)(6)`).
  - *Sanitization (`sanitize_document`):* Strip `/Info`, `/Metadata` XMP streams, `/Names /JavaScript`, `/OpenAction`, `/AA`, `/Launch`, `/Names /EmbeddedFiles`, `/Annots` comments/form data, `/OCProperties` hidden layers, `/PieceInfo`, page `/Thumb` thumbnails, and compact the cross-reference table via full rewrite so zero deleted historical objects remain in the file.
- Expose `recto-cli sign`, `recto-cli verify`, `recto-cli redact`, and `recto-cli sanitize`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement X.509 / PKCS#12 / Windows Cert Store signing (`PKCS#7` and `PAdES`), `/ByteRange` incremental-save serialization, visible `/AP` signature appearance synthesis, RFC 3161 timestamping, `/DSS` embedding, and signature verification in `src/pdf/signature.cpp`, plus `recto-cli sign` and `recto-cli verify`. Done when: `PdfSignature.*` generates a test X.509 keypair via CNG, signs a PDF twice via incremental save (two counter-signatures), verifies both signatures as valid, and detects tampering if a single byte outside `/Contents` is modified.
2. Implement physical content-stream redaction (glyph-run splitting, vector clipping, image pixel scrubbing, burn-in overlay) and document sanitization in `src/pdf/redactor.cpp`, plus `recto-cli redact` and `recto-cli sanitize`. Done when: `PdfRedactor.*` redacts a target word and an image region, and verifies that neither `PageText` extraction nor raw decompressed stream byte search finds the redacted characters anywhere in the output file under ASan.

- [ ] Cryptographic PDF signing and verification (`src/pdf/signature.cpp`: PKCS#12 & Windows Cert Store, `PKCS#7` & `PAdES`, `/DocMDP`, visible `/AP`, incremental multi-signing). Done when: `PdfSignature.*` passes under ASan covering single sign, multi-sign incremental save, verification, and tamper detection. Cheaper substitute: drawing a visual signature stamp without a cryptographic `/ByteRange` CMS signature dictionary.
- [ ] True physical content-stream redaction and one-click document sanitization (`src/pdf/redactor.cpp`). Done when: `PdfRedactor.*` proves zero residual redacted bytes in decompressed streams and complete removal of hidden metadata/scripts/layers after sanitization. Cheaper substitute: drawing a black rectangle over text while leaving the underlying `Tj`/`TJ` string in the content stream.
- [ ] Commit: `pdf: add cryptographic digital signatures (PKCS#7/PAdES) and true physical redactor`

**Test checkpoint:** `recto-tests --gtest_filter=PdfSignature.*:PdfRedactor.*` passes under ASan.

## Verification

- M4's merge, split, reorder, rotate, delete, blank-page insert, crop/resize, watermarks, headers/footers, Bates numbering, encrypt/decrypt, digital signatures, and true redaction/sanitization each pass under ASan and round-trip in `D06 T01 §3`'s suite.
- No operation path modifies the open viewer's document instance in place.
- `python scripts/todo-graph.py validate` passes.
