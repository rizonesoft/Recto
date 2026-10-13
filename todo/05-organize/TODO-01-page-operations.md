---
schema_version: 1
id: page-operations
domain: 05-organize
status: active
title: "TODO-01 -- Page Operations: Merge, Split, Reorder, Encrypt"
track: M4
---

# TODO-01 -- Page Operations: Merge, Split, Reorder, Encrypt

> **Goal:** The engine-side page surgery behind Organize and the Convert cards: graft-based merge and split, reorder, rotate, delete and extract on a copy, and AES-256 encrypt and decrypt, all as `recto-core` operations the job frame runs, never modifying an open viewer document in place.

> **Current state:** Measured 2026-10-13. No code exists. The mechanisms are plan section 5's rows: `pdf_create_document` with one `pdf_graft_map` per source for merge and split, page-tree functions on a copy for reorder, rotate and delete, `pdf_write_options` with `PDF_ENCRYPT_AES_256` for encryption, all saved through the atomic rule. Bridge work follows D01 T01's status pattern in a new `src/bridge/pdf_ops.c`; D04 T02 §1's writer plumbing and output helper are shared by agreement.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` section 5 (Merge, Split, Rotate/delete/reorder, Encrypt/decrypt rows and the stretch goal of source bookmarks as outline entries), section 4.9 (job isolation).
- `design/SPEC.md` sections 9.5 (One combined PDF, Split files, Protected and Unlocked cards) and 9.6 (the operations the Organize UI performs).
- -> XREF: D04 T02 §1 -- the shared writer plumbing and atomic save helper this file consumes; graft-map needs (per-source maps) feed back into that plumbing's shape.
- -> XREF: D05 T02 §1 -- the Organize grid whose edits compile down to this file's operation list; the operation vocabulary is agreed here.

## Outcome

- Merging N PDFs yields one PDF with pages in the stated order, byte-level page content preserved by grafting (no re-render), verifiable page by page.
- Splitting by ranges, every N, or per page yields exactly the stated files; reorder, rotate, delete and extract round-trip through render comparison.
- Encryption produces AES-256 files with the stated permissions that other readers honor; decryption with the right password produces a clean unprotected copy, and the wrong password is a typed refusal.

**Adjacency:** all=not-applicable (engine operations inside the D04 T01 job frame; cards, queue, notices and save settings are owned there and by D02 T04 §2)

**Adjacency rationale:** This file is the verb library the Convert cards and the Organize UI call. Its surfaces are other files' surfaces: the cards and grid (D04 T01, D05 T02), the queue, and the save dialogs. Permissions in the PDF sense (print, copy, edit flags) are data the encrypt operation writes, exercised in section 3's tests, not an app permission system.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Graft merge and split | D04 T02 §1 | [ ] |
| 2 | §2 | Reorder, rotate, delete and extract on a copy | §1 | [ ] |
| 3 | §3 | Encrypt and decrypt (AES-256) | §1 | [ ] |

## 1. Graft merge and split

Merge by grafting mapped pages from each source into a fresh document (object-level copy, shared resources de-duplicated by the graft map), preserving each page's content and rotation; split is the same machinery into several documents by range spec (explicit ranges, every N, one per page). Source outlines become top-level entries naming the source file where the stretch goal is taken for v1 (decide and record in this section when building; the Decide is cheap here and expensive in the UI). Encrypted sources flow through the job's password recovery (D04 T01 §1), never a bridge-level prompt.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/bridge/pdf_ops.c` graft functions and `src/core/page_ops.cpp` merge and split operations over a source list with per-source page selections. Done when: merging three corpus files and splitting the result back by the original ranges reproduces pages that pixel-match the originals through `recto-cli render`.
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

1. Implement the operation list in `src/core/page_ops.cpp` over §1's graft machinery (a permutation is a single-source graft in a new order; rotation writes the page `/Rotate` delta through the bridge). Done when: a scripted edit (move page 3 to front, rotate page 2 by 90, delete page 5) renders exactly as specified against reference renders.
2. Extract as its own named operation for the UI's "Extract to new PDF". Done when: extracting a middle range equals splitting to that range, byte-comparable renders.

- [ ] Operation list application on a copy. Done when: applying an empty list reproduces the source's renders page for page. Cheaper substitute: mutating the loaded document and saving it, which plan section 5 forbids for the viewer document.
- [ ] Rotation persisted as page rotation, not re-rendered content. Done when: the output's page boxes report the rotated dimensions through `recto-cli info`.
- [ ] Commit: `organize: reorder, rotate, delete and extract as copy operations`

**Test checkpoint:** `recto-tests --gtest_filter=PageOps.*` green under ASan with the scripted-edit render comparisons quoted.

## 3. Encrypt and decrypt (AES-256)

Encrypt: save a copy with `PDF_ENCRYPT_AES_256`, user and owner passwords, and the permission set the UI chose (print, copy, edit, annotate), offered as the Protected PDF card. Decrypt: open with the supplied password and save an unprotected copy (the Unlocked card). Both follow the atomic rule and the SPEC 9.9 Replace original flow when the target is the source; the viewer document is never the thing being saved (plan section 5's note).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Bridge save options (`pdf_write_options` through `src/bridge/pdf_ops.c`) and core encrypt and decrypt operations with typed password outcomes through the job contract. Done when: the Permissions-and-security readback (D02 T03 §3's bridge addition) reports AES-256 and exactly the permissions chosen, and a stock reader enforces a denied print permission on the output.
2. Decrypt round-trip: encrypt a corpus file, decrypt the result, renders equal the original. Done when: wrong-password decrypt is the typed refusal, never a partial file.

- [ ] AES-256 encrypt with permissions. Done when: the Info panel's security summary (D02 T03 §3) reads back method and permissions accurately. Cheaper substitute: the legacy 128-bit RC4 default a careless options struct gives.
- [ ] Decrypt with typed password outcomes. Done when: the queue's Enter password action (D04 T01 §3) completes a decrypt end to end.
- [ ] Commit: `organize: AES-256 encrypt and decrypt with permissions`

**Test checkpoint:** `recto-tests --gtest_filter=PageOps.*` green under ASan: encrypt-decrypt render equality, permission readback, wrong-password refusal (quoted).

## Verification

- M4's merge, split, reorder, rotate, delete, encrypt and decrypt rows (plan section 7 via section 5) each round-trip the corpus with render-level evidence, ready for D06 T01 §3's suite.
- No operation path opens the viewer's document instance (isolation re-proven in §3's queue flow).
- `python scripts/todo-graph.py validate` passes.
