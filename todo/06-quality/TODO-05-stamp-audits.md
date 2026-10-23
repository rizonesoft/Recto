---
schema_version: 1
id: stamp-audits
domain: 06-quality
status: active
title: "TODO-05 -- Audits of Sections Stamped Before the Current Review Process"
---

# TODO-05 -- Audits of Sections Stamped Before the Current Review Process

> **Goal:** Every section stamped before 2026-10-21 is re-verified under the current review process before later work builds further on it: its own Test checkpoint re-run at the current tip, and fresh independent audit rounds by the pinned reviewer with a 22-round cap and the severity cutoff. A confirmed stamp stays; a regression flips its row back with a dated blocking note, a filed repair and a list of suspect downstream stamps.

> **Groomed 2026-10-23:** drift and sequence only; no audit verdict or stamped checklist was touched. `D01 T02 §4` now waits for `§32` and `§33` (image decode repairs), and `D01 T03 §5` (the 1-hour fuzz run) waits for `§29` to `§35`, so fuzzing starts on decoders whose known gaps are closed. `§22` is mid-audit (round 4 reached, 22-round cap, severity cutoff from round 9).

> **Current state:** Measured 2026-10-21. 24 sections carry stamps from the earlier process (`python scripts/todo-graph.py query rounds` lists them with `round_cap` null): 14 in `00-workspace` and 10 in `01-engine`. They were reviewed under a three-round cap by a different reviewer; the operator asked on 2026-10-21 for them to be brought up to the current standard. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI references rewritten against `todo/10-ui/`.

## Inputs

- `.claude/skills/review-todo-section/SKILL.md` (audit stance, the review-round cap and the severity cutoff) and `docs/review-round-prompt.md` (severity tags).
- Each audited section's own contract and receipt (`python scripts/todo-graph.py resolve "<ref>"`, `docs/reviews/<ref>.json`).
- The run file `docs/phase-runs/2026-10-21-phase-0.md` (the operator's request).
- -> XREF: D10 T01 §2 -- replaces the WinUI scaffold of `D00 T01 §3` that §3 here audited.

## Outcome

- Every section stamped before 2026-10-21 is either re-confirmed with an audit findings file under `docs/reviews/06-quality/`, or flipped back with a dated blocking note, a filed repair and its suspect downstream stamps listed.
- `D01 T02 §1` and `D01 T02 §5`, the next engine work, wait for the engine audits.

**Adjacency:** all=not-applicable (review records over shipped sections: no product data, settings, exchange or undo of their own)

**Adjacency rationale:** An audit produces findings files and, on a regression, a blocking note and a repair section in the audited TODO file; none of the adjacency kinds applies to the audit itself.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Audit D00 T01 §1: Git repository and repo-face files | D00 T01 §1 | [x] |
| 2 | §2 | Audit D00 T01 §2: PDF engine dependency policy (native recto-pdf, MuPDF retired) | D00 T01 §2, §1 | [x] |
| 3 | §3 | Audit D00 T01 §3: Solution scaffold for all modules, x64 and ARM64 | D00 T01 §3, §2 | [x] |
| 4 | §4 | Audit D00 T01 §5: Reference benchmark machines recorded | D00 T01 §5, §3 | [x] |
| 5 | §5 | Audit D00 T02 §1: Graph engine, CLI and tests | D00 T02 §1, §4 | [x] |
| 6 | §6 | Audit D00 T02 §2: Skills, rules and the CI gate | D00 T02 §2, §5 | [x] |
| 7 | §7 | Audit D00 T01 §4: Build CI for x64 and ARM64 | D00 T01 §4, §6 | [x] |
| 8 | §8 | Audit D00 T02 §3: Seeded graph and generated plan | D00 T02 §3, §7 | [x] |
| 9 | §9 | Audit D00 T03 §1: test_support corpus path resolver and TempDir RAII helper | D00 T03 §1, §8 | [x] |
| 10 | §10 | Audit D00 T03 §2: RGBA pixel comparison (expect_rgba_equal, compute_ssim) and test | D00 T03 §2, §9 | [x] |
| 11 | §11 | Audit D00 T03 §3: Deterministic PDF corpus generator (scripts/build-corpus.py) for | D00 T03 §3, §10 | [x] |
| 12 | §12 | Audit D00 T03 §4: Encrypted (RC4/AES-128/AES-256), broken/ and seeds/ corpus fixtures | D00 T03 §4, §11 | [x] |
| 13 | §13 | Audit D00 T03 §5: tests/corpus/PROVENANCE.md and automated Corpus.Provenance SHA-256 | D00 T03 §5, §12 | [x] |
| 14 | §14 | Audit D00 T03 §6: MSVC AddressSanitizer (/fsanitize=address) build configuration and CI | D00 T03 §6, §13 | [x] |
| 15 | §15 | Audit D01 T01 §1: PdfResult<T>, PdfError, ErrorCode, D16 diagnostic ring and ETW | D01 T01 §1, §14 | [x] |
| 16 | §16 | Audit D01 T01 §2: PdfValue variant object model, dictionary/array helpers and deep | D01 T01 §2, §15 | [x] |
| 17 | §17 | Audit D01 T01 §3: Binary-safe PDF 1.7 and 2.0 lexer (src/pdf/lexer.cpp) | D01 T01 §3, §16 | [x] |
| 18 | §18 | Audit D01 T01 §4: Bounded recursive-descent indirect object parser (src/pdf/parser.cpp) | D01 T01 §4, §17 | [x] |
| 19 | §19 | Audit D01 T01 §5: Classic xref tables, trailer merging and /Prev incremental update | D01 T01 §5, §18 | [x] |
| 20 | §20 | Audit D01 T01 §7: Lossless stream filters (Flate, LZW, predictors, ASCII85, ASCIIHex | D01 T01 §7, §19 | [x] |
| 21 | §21 | Audit D01 T01 §6: PDF 1.5+ /Type /XRef streams, /Type /ObjStm streams and damaged-file | D01 T01 §6, §20 | [x] |
| 22 | §22 | Audit D01 T01 §8: Bi-level and image stream filters (CCITTFaxDecode, JBIG2Decode, WIC | D01 T01 §8, §21 | [x] |
| 23 | §23 | Audit D01 T01 §9: Standard Security Handler decryption (RC4, AES-128, AES-256 R=2..6 | D01 T01 §9, §22 | [x] |
| 24 | §24 | Audit D01 T01 §10: pdf::Document page-tree resolver, metadata, /Outlines, font list and | D01 T01 §10, §23 | [ ] |
| 25 | §25 | Retired MuPDF and AGPL text removed from the A, B and C mockup boards | §2 | [x] |
| 26 | §26 | Corpus validator filter decoders conform to ISO 32000-1 7.4 | §12 | [x] |
| 27 | §27 | ARM64 Debug build free of the CS1668 LIB search-path warnings | §13 | [x] |
| 28 | §28 | Retired Office and LibreOffice converter surfaces removed from the A, B and C boards | §25 | [x] |
| 29 | §29 | Damaged-file recovery resolves indirect object-stream parameters | §21 | [ ] |
| 30 | §30 | Damaged-file recovery keeps an encrypted file's compressed objects | §21 | [ ] |
| 31 | §31 | JBIG2 striped pages, MMR halftone alignment and HENABLESKIP | §22 | [ ] |
| 32 | §32 | JPX per-component precision and sampling, and the full coding-style set | §22 | [ ] |
| 33 | §33 | DCT /ColorTransform and Adobe APP14 CMYK and YCCK decoding | §22 | [ ] |
| 34 | §34 | CCITT damaged-row recovery as ISO 32000-1 Table 11 specifies | §22 | [ ] |
| 35 | §35 | JBIG2 decoder work and memory bounded at every allocation and loop | §22 | [ ] |
| 36 | §36 | Native JPX decoder bounded by a cumulative work budget | §22 | [ ] |
| 37 | §37 | WIC decode failures classified by HRESULT, not reported as damage | §22 | [ ] |
| 38 | §38 | DCT decodes only the pixel representation the caller asked for | §22 | [ ] |
| 39 | §39 | Per-codec decode latency measured on valid, large and damaged fixtures | §22 | [ ] |
| 40 | §40 | Image decoder allocations drawn from one budgeted memory resource per decode | §22 | [ ] |
| 41 | §41 | Recovery resolves compressed parameter targets and an indirect container /Type | §29 | [ ] |
| 42 | §42 | Cancellable damaged-file recovery with progress | §29 | [ ] |
| 43 | §43 | Repaired copy of a recovered file, and salvage of a damaged page tree | §29 | [ ] |
| 44 | §44 | Damaged-file recovery measured against competitors on one corpus | §29 | [ ] |
| 45 | §45 | Encrypted recovery without an intact xref stream: containers read after authentication | §30 | [ ] |
| 46 | §46 | Attachment-only encryption: public pages open, `/EFOpen` asks only for attachments | §23 | [ ] |
| 47 | §47 | Standard security handler interoperability matrix and cancellable decryption | §23 | [ ] |
| 48 | §48 | JBIG2 interoperability fixtures from an independent encoder, Huffman retained contexts | §31 | [ ] |

## 1. Audit D00 T01 §1: Git repository and repo-face files

> **Started:** 2026-10-21T14:10:00Z

`D00 T01 §1` (Git repository and repo-face files) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t01-s1.json` records 2 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Re-run `D00 T01 §1`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t01-s1.md`.
2. Audit `D00 T01 §1` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t01-s1.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T01 §1"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t01-s1.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T01 §1`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t01-s1.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t01-s1.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T01 §1`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t01-s1.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T01 §1 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T01 §1"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t01-s1.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §1 | e68e94d0832c099403f90d549d3b5a34a5e406ef
> **Review:** `docs/reviews/d06-t05-s1.json`

## 2. Audit D00 T01 §2: PDF engine dependency policy (native recto-pdf, MuPDF retired)

> **Started:** 2026-10-21T14:40:00Z

`D00 T01 §2` (PDF engine dependency policy (native `recto-pdf`, MuPDF retired)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t01-s2.json` records 2 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T01 §2`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t01-s2.md`.
2. Audit `D00 T01 §2` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t01-s2.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T01 §2"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t01-s2.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T01 §2`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t01-s2.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t01-s2.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T01 §2`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t01-s2.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T01 §2 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T01 §2"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t01-s2.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §2 | d08190ad584a566b3d9c043f868d5c267feac32a
> **Review:** `docs/reviews/d06-t05-s2.json`

## 3. Audit D00 T01 §3: Solution scaffold for all modules, x64 and ARM64

> **Started:** 2026-10-21T16:20:00Z

`D00 T01 §3` (Solution scaffold for all modules, x64 and ARM64) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t01-s3.json` records 1 rounds (clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T01 §3`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t01-s3.md`.
2. Audit `D00 T01 §3` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t01-s3.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T01 §3"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t01-s3.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T01 §3`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t01-s3.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t01-s3.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T01 §3`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t01-s3.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T01 §3 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T01 §3"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t01-s3.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §3 | bf66a966e36f036d20461da54fe15d59f65847dd
> **Review:** `docs/reviews/d06-t05-s3.json`

> **Corrected 2026-10-22:** WinUI 3 is dropped for Recto's own UI toolkit `recto-ui` (foundation plan D4). The WinUI scaffold of `D00 T01 §3` that this section audited is replaced by `D10 T01 §2`; the audit verdict covers the scaffold as built and is unchanged.

## 4. Audit D00 T01 §5: Reference benchmark machines recorded

> **Started:** 2026-10-21T17:05:00Z

`D00 T01 §5` (Reference benchmark machines recorded) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t01-s5.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T01 §5`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t01-s5.md`.
2. Audit `D00 T01 §5` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t01-s5.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T01 §5"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t01-s5.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T01 §5`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t01-s5.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t01-s5.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T01 §5`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t01-s5.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T01 §5 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T01 §5"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t01-s5.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §4 | 811fefe27721521c5ab44e99d4fadc3b186a4435
> **Review:** `docs/reviews/d06-t05-s4.json`

## 5. Audit D00 T02 §1: Graph engine, CLI and tests

> **Started:** 2026-10-21T17:40:00Z

`D00 T02 §1` (Graph engine, CLI and tests) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t02-s1.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Re-run `D00 T02 §1`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t02-s1.md`.
2. Audit `D00 T02 §1` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t02-s1.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T02 §1"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t02-s1.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T02 §1`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t02-s1.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t02-s1.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T02 §1`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t02-s1.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T02 §1 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T02 §1"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t02-s1.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §5 | 995b6fbc64f5745f723c1b2740108816b3318d78
> **Review:** `docs/reviews/d06-t05-s5.json`

## 6. Audit D00 T02 §2: Skills, rules and the CI gate

> **Started:** 2026-10-21T19:10:00Z

`D00 T02 §2` (Skills, rules and the CI gate) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t02-s2.json` records 2 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Re-run `D00 T02 §2`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t02-s2.md`.
2. Audit `D00 T02 §2` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t02-s2.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T02 §2"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t02-s2.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T02 §2`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t02-s2.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t02-s2.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T02 §2`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t02-s2.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T02 §2 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T02 §2"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t02-s2.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §6 | 7fb3a2ab75ab386713db3146fc9b5b3df5969e43
> **Review:** `docs/reviews/d06-t05-s6.json`

## 7. Audit D00 T01 §4: Build CI for x64 and ARM64

> **Started:** 2026-10-21T20:05:00Z

`D00 T01 §4` (Build CI for x64 and ARM64) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t01-s4.json` records 2 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T01 §4`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t01-s4.md`.
2. Audit `D00 T01 §4` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t01-s4.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T01 §4"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t01-s4.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T01 §4`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t01-s4.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t01-s4.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T01 §4`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t01-s4.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T01 §4 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T01 §4"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t01-s4.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §7 | e41f2ce57c591ce7f16767f6b6b7c988db293a89
> **Review:** `docs/reviews/d06-t05-s7.json`

## 8. Audit D00 T02 §3: Seeded graph and generated plan

> **Started:** 2026-10-21T21:00:00Z

`D00 T02 §3` (Seeded graph and generated plan) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t02-s3.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Re-run `D00 T02 §3`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t02-s3.md`.
2. Audit `D00 T02 §3` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t02-s3.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T02 §3"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t02-s3.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T02 §3`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t02-s3.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t02-s3.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T02 §3`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t02-s3.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T02 §3 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T02 §3"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t02-s3.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §8 | 5e4b26610421ec2ab6d0cccc324df87b981ae60b
> **Review:** `docs/reviews/d06-t05-s8.json`

## 9. Audit D00 T03 §1: test_support corpus path resolver and TempDir RAII helper

> **Started:** 2026-10-21T21:40:00Z

`D00 T03 §1` (`test_support` corpus path resolver and `TempDir` RAII helper) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t03-s1.json` records 5 rounds (fixed, fixed, fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T03 §1`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t03-s1.md`.
2. Audit `D00 T03 §1` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t03-s1.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T03 §1"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t03-s1.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T03 §1`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t03-s1.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t03-s1.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T03 §1`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t03-s1.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T03 §1 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T03 §1"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t03-s1.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §9 | d00cb2e28de7ec1cdb09145c6f3cd0f51ee37b2b
> **Review:** `docs/reviews/d06-t05-s9.json`

## 10. Audit D00 T03 §2: RGBA pixel comparison (expect_rgba_equal, compute_ssim) and test

> **Started:** 2026-10-21T22:30:00Z

`D00 T03 §2` (RGBA pixel comparison (`expect_rgba_equal`, `compute_ssim`) and test assertion helpers) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t03-s2.json` records 2 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T03 §2`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t03-s2.md`.
2. Audit `D00 T03 §2` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t03-s2.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T03 §2"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t03-s2.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T03 §2`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t03-s2.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t03-s2.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T03 §2`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t03-s2.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T03 §2 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T03 §2"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t03-s2.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §10 | 50c635897a6b859fa22251aba0eedfd9298ccc6d
> **Review:** `docs/reviews/d06-t05-s10.json`

## 11. Audit D00 T03 §3: Deterministic PDF corpus generator (scripts/build-corpus.py) for

> **Started:** 2026-10-21T23:20:00Z

`D00 T03 §3` (Deterministic PDF corpus generator (`scripts/build-corpus.py`) for `render/` and `text/`) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t03-s3.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T03 §3`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t03-s3.md`.
2. Audit `D00 T03 §3` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t03-s3.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T03 §3"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t03-s3.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T03 §3`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t03-s3.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t03-s3.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T03 §3`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t03-s3.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T03 §3 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T03 §3"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t03-s3.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-21 | §11 | daf5ca94da3c99f209817031db7219e3ab026ef0
> **Review:** `docs/reviews/d06-t05-s11.json`

## 12. Audit D00 T03 §4: Encrypted (RC4/AES-128/AES-256), broken/ and seeds/ corpus fixtures

> **Started:** 2026-10-22T00:40:00Z
>
> **Resolved:** a systematic pass over every corpus validator stream decoder against ISO 32000-1 7.4, the pattern rounds 19 to 22 found one decoder at a time -> XREF: D06 T05 §26 (2026-10-22: closed by D06 T05 §26, which reads every validator decoder against 7.4 and pins each image codec to the corpus form)

`D00 T03 §4` (Encrypted (`RC4`/`AES-128`/`AES-256`), `broken/` and `seeds/` corpus fixtures) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t03-s4.json` records 2 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T03 §4`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t03-s4.md`.
2. Audit `D00 T03 §4` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t03-s4.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T03 §4"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t03-s4.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T03 §4`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t03-s4.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t03-s4.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T03 §4`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t03-s4.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T03 §4 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T03 §4"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t03-s4.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §12 | d4c2c4eb66919e6c322537c812ce412f38672aa2
> **Review:** `docs/reviews/d06-t05-s12.json`

## 13. Audit D00 T03 §5: tests/corpus/PROVENANCE.md and automated Corpus.Provenance SHA-256

> **Started:** 2026-10-22T02:10:00Z
>
> **Resolved:** the 4 MSBuild CS1668 warnings in the ARM64 Debug `recto-tests` build, which break `D00 T01 §3`'s zero-warnings checkpoint -> XREF: D06 T05 §27 (2026-10-22: closed by D06 T05 §27, which keeps only existing library folders on LIB and fails build.ps1 on any MSBuild warning)

`D00 T03 §5` (`tests/corpus/PROVENANCE.md` and automated `Corpus.Provenance` SHA-256 integrity suite) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t03-s5.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T03 §5`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t03-s5.md`.
2. Audit `D00 T03 §5` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t03-s5.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T03 §5"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t03-s5.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T03 §5`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t03-s5.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t03-s5.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T03 §5`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t03-s5.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T03 §5 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T03 §5"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t03-s5.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §13 | 3c3b871460eb492dbe60c85ae3cba3ade989a37c
> **Review:** `docs/reviews/d06-t05-s13.json`

## 14. Audit D00 T03 §6: MSVC AddressSanitizer (/fsanitize=address) build configuration and CI

> **Started:** 2026-10-22T03:40:00Z

`D00 T03 §6` (MSVC AddressSanitizer (`/fsanitize=address`) build configuration and CI `asan` job) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d00-t03-s6.json` records 2 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D00 T03 §6`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d00-t03-s6.md`.
2. Audit `D00 T03 §6` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d00-t03-s6.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D00 T03 §6"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d00-t03-s6.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D00 T03 §6`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d00-t03-s6.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d00-t03-s6.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D00 T03 §6`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d00-t03-s6.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D00 T03 §6 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D00 T03 §6"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d00-t03-s6.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §14 | 0d9683a2bf279ceb2ca0aa089aa70e3ded5dde3b
> **Review:** `docs/reviews/d06-t05-s14.json`

## 15. Audit D01 T01 §1: PdfResult<T>, PdfError, ErrorCode, D16 diagnostic ring and ETW

> **Started:** 2026-10-22T16:20:00Z

`D01 T01 §1` (`PdfResult<T>`, `PdfError`, `ErrorCode`, D16 diagnostic ring and ETW `TraceLogging`) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s1.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §1`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s1.md`.
2. Audit `D01 T01 §1` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s1.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §1"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s1.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §1`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s1.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s1.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §1`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s1.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §1 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §1"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s1.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §15 | be99c41b93d924225fcc375904b51a42fb90f342
> **Review:** `docs/reviews/d06-t05-s15.json`

## 16. Audit D01 T01 §2: PdfValue variant object model, dictionary/array helpers and deep

> **Started:** 2026-10-22T08:00:00Z

`D01 T01 §2` (`PdfValue` variant object model, dictionary/array helpers and deep cloning) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s2.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §2`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s2.md`.
2. Audit `D01 T01 §2` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s2.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §2"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s2.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §2`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s2.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s2.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §2`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s2.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §2 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §2"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s2.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §16 | f9c1b2a6596bdfb91ba2152c9bc9b89fa50e5f8a
> **Review:** `docs/reviews/d06-t05-s16.json`

## 17. Audit D01 T01 §3: Binary-safe PDF 1.7 and 2.0 lexer (src/pdf/lexer.cpp)

> **Started:** 2026-10-22T09:10:00Z

`D01 T01 §3` (Binary-safe PDF 1.7 and 2.0 lexer (`src/pdf/lexer.cpp`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s3.json` records 3 rounds (fixed, fixed, fixed). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §3`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s3.md`.
2. Audit `D01 T01 §3` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s3.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §3"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s3.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §3`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s3.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s3.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §3`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s3.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §3 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §3"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s3.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §17 | 9d140561841f253c313904af97e6865f3ff000fe
> **Review:** `docs/reviews/d06-t05-s17.json`

## 18. Audit D01 T01 §4: Bounded recursive-descent indirect object parser (src/pdf/parser.cpp)

> **Started:** 2026-10-22T09:50:00Z

`D01 T01 §4` (Bounded recursive-descent indirect object parser (`src/pdf/parser.cpp`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s4.json` records 3 rounds (fixed, fixed, fixed). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §4`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s4.md`.
2. Audit `D01 T01 §4` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s4.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §4"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s4.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §4`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s4.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s4.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §4`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s4.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §4 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §4"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s4.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §18 | 5b322e7c097eec06044b7bd35e779c97515997fa
> **Review:** `docs/reviews/d06-t05-s18.json`

## 19. Audit D01 T01 §5: Classic xref tables, trailer merging and /Prev incremental update

> **Started:** 2026-10-22T10:40:00Z

`D01 T01 §5` (Classic `xref` tables, trailer merging and `/Prev` incremental update chains) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s5.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §5`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s5.md`.
2. Audit `D01 T01 §5` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s5.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §5"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s5.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §5`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s5.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s5.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §5`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s5.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §5 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §5"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s5.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §19 | a527f8a289e875756de219e71abbb81e7ddf04d5
> **Review:** `docs/reviews/d06-t05-s19.json`

## 20. Audit D01 T01 §7: Lossless stream filters (Flate, LZW, predictors, ASCII85, ASCIIHex

> **Started:** 2026-10-22T11:20:00Z

`D01 T01 §7` (Lossless stream filters (`Flate`, `LZW`, predictors, `ASCII85`, `ASCIIHex`, `RunLength`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s7.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §7`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s7.md`.
2. Audit `D01 T01 §7` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s7.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §7"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s7.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §7`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s7.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s7.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §7`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s7.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §7 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §7"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s7.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §20 | 03839c42d13e229c55a9078012c4f9ac2bd4d9d9
> **Review:** `docs/reviews/d06-t05-s20.json`

## 21. Audit D01 T01 §6: PDF 1.5+ /Type /XRef streams, /Type /ObjStm streams and damaged-file

> **Started:** 2026-10-22T14:30:00Z

`D01 T01 §6` (PDF 1.5+ `/Type /XRef` streams, `/Type /ObjStm` streams and damaged-file recovery) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s6.json` records 10 rounds (fixed, fixed, fixed, fixed, fixed, fixed, fixed, fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §6`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s6.md`.
2. Audit `D01 T01 §6` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s6.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §6"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s6.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §6`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s6.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s6.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §6`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s6.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §6 under the current review process`

> **Deferred:** damaged-file recovery decoding object streams whose `/Filter`, `/DecodeParms`, `/N` or `/First` is indirect, which needs the recovery scan to defer unpacking -> XREF: D06 T05 §29

> **Deferred:** damaged-file recovery of an encrypted file's compressed objects (xref-stream rows dropped, containers decoded before authentication) -> XREF: D06 T05 §30

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §6"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s6.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-22 | §21 | a0bb2abafef79db982e9b628d9092fdf0c5ec67f
> **Review:** `docs/reviews/d06-t05-s21.json`

## 22. Audit D01 T01 §8: Bi-level and image stream filters (CCITTFaxDecode, JBIG2Decode, WIC

> **Started:** 2026-10-22T19:40:00Z

`D01 T01 §8` (Bi-level and image stream filters (`CCITTFaxDecode`, `JBIG2Decode`, WIC `DCT` and `JPX`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s8.json` records 16 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §8`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s8.md`.
2. Audit `D01 T01 §8` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s8.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §8"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s8.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §8`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s8.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s8.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §8`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s8.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp. Confirmed 2026-10-23: no regression since the stamp; the audit's findings were defects present at the stamped candidate, fixed in place in `88f54a4` to `716eaa5` or filed as `D06 T05 §31` to `§40`, so `D01 T01 §8` keeps its stamp.
- [x] Commit: `review: audit D01 T01 §8 under the current review process`

> **Deferred:** JBIG2 striped pages, MMR halftone plane alignment and HENABLESKIP -> XREF: D06 T05 §31

> **Deferred:** JPX per-component precision and sampling, and the full coding-style set -> XREF: D06 T05 §32

> **Deferred:** DCT /ColorTransform and Adobe APP14 CMYK and YCCK decoding -> XREF: D06 T05 §33

> **Deferred:** CCITT damaged-row recovery per ISO 32000-1 Table 11 -> XREF: D06 T05 §34

> **Deferred:** a systematic JBIG2 work-and-memory pass, and the regression fixtures for the round-3 reference-charging, compositing-clip and unsigned HRX/HRY fixes -> XREF: D06 T05 §35

> **Deferred:** a cumulative work budget for the native JPX decoder -> XREF: D06 T05 §36

> **Deferred:** WIC failures classified by HRESULT -> XREF: D06 T05 §37

> **Deferred:** DCT decoding only the requested pixel representation -> XREF: D06 T05 §38

> **Deferred:** measured per-codec decode latency -> XREF: D06 T05 §39

> **Deferred:** every image decoder allocation drawn from one budgeted memory resource, replacing per-site estimates -> XREF: D06 T05 §40

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §8"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s8.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-23 | §22 | 716eaa5acd0bac73f8af681dc8ef6a02370274fe
> **Review:** `docs/reviews/d06-t05-s22.json`

## 23. Audit D01 T01 §9: Standard Security Handler decryption (RC4, AES-128, AES-256 R=2..6

> **Started:** 2026-10-23T10:05:00Z

`D01 T01 §9` (Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6` via CNG)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s9.json` records 5 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §9`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s9.md`.
2. Audit `D01 T01 §9` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s9.md` ends in an approving round or a recorded stop with every finding owned.

- [x] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §9"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s9.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [x] Run audit rounds on `D01 T01 §9`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s9.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [x] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s9.md` is open without a fix commit or a filed section.
- [x] On a confirmed regression, add a dated blocking note to `D01 T01 §9`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s9.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [x] Commit: `review: audit D01 T01 §9 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §9"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s9.md` ends in an approving round or a recorded stop with every finding fixed or filed.

> **Verified:** 2026-10-23 | §23 | 9aeb2cfae8df7bd8ab1cb846230f3caf1a8a421f
> **Review:** `docs/reviews/d06-t05-s23.json`

## 24. Audit D01 T01 §10: pdf::Document page-tree resolver, metadata, /Outlines, font list and

`D01 T01 §10` (`pdf::Document` page-tree resolver, metadata, `/Outlines`, font list and `/Annots` links) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s10.json` records 14 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §10`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s10.md`.
2. Audit `D01 T01 §10` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s10.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §10"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s10.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §10`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s10.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s10.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §10`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s10.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §10 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §10"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s10.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 25. Retired MuPDF and AGPL text removed from the A, B and C mockup boards

> **Started:** 2026-10-22T05:10:00Z
>
> **Resolved:** the retired Office and LibreOffice converter dialog and setting on the A, B and C boards, a design choice beyond a text swap -> XREF: D06 T05 §28 (2026-10-22: closed by D06 T05 §28, which replaces them with SPEC 9.9's redaction confirm and SPEC 9.7's page-setup row)

The audit of `D00 T01 §2` (`docs/reviews/06-quality/audit-d00-t01-s2.md`) found the older A, B and C mockup boards under `design/mockups/source/` and their rendered `design/mockups/html/` copies still showing MuPDF as the engine and AGPL-3.0 as the licence (`A-About` lists MuPDF, jbig2dec and extract under AGPL, `A-Banner` shows `MuPDF` and `AGPL-3.0` chips, and about twenty boards in all mention MuPDF). Recto's engine is its own `recto-pdf` with zero MuPDF (D3, D9) and the licence is GPL-3.0-or-later (D2), so these boards contradict the plan. The `D-` boards are the current design; this section brings the older boards in line rather than leaving them to mislead.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Edit every `design/mockups/source/[ABC]-*.dc.html` that names MuPDF, AGPL or a MuPDF-only component. Done when: `git grep -n -i -E "mupdf|agpl|jbig2dec|artifex" -- design/mockups/source` prints nothing.
2. Re-render the edited boards with `node design/tools/render-mockups.mjs --only <names>` (**Corrected 2026-10-22:** the renderer had no `--only`; this section added it). Done when: `git grep -n -i -E "mupdf|agpl" -- design/mockups/html design/screenshots` prints nothing and each edited board's PNG is newer than its source.

- [x] Replace MuPDF with `recto-pdf` and AGPL-3.0 with GPL-3.0-or-later in every `design/mockups/source/[ABC]-*.dc.html`, and drop MuPDF-only third-party rows (jbig2dec, extract, Artifex) from the About boards' licence lists, keeping the rows for components Recto still ships. Done when: `git grep -n -i -E "mupdf|agpl|jbig2dec|artifex" -- design/mockups/source` prints nothing.
- [x] Re-render those boards through `design/tools/render-mockups.mjs --only` so `design/mockups/html/` and `design/screenshots/` match their sources. Done when: `git grep -n -i -E "mupdf|agpl" -- design/mockups/html` prints nothing and the boards' PNG files are newer than their sources.
- [x] Add `tests/scripts/test_engine_dependency_policy.py`'s design check (no MuPDF or AGPL text in `design/mockups/`). Done when: restoring one MuPDF chip in a source makes the test fail.
- [x] Commit: `design: retire MuPDF and AGPL text from the A, B and C mockup boards`

**Test checkpoint:** `python -m unittest tests.scripts.test_engine_dependency_policy` passes, and `git grep -n -i -E "mupdf|agpl" -- design/mockups` prints nothing.

> **Verified:** 2026-10-22 | §25 | 532df218d48d36006d02f1b649761b5bd8cbe567
> **Review:** `docs/reviews/d06-t05-s25.json`

## 26. Corpus validator filter decoders conform to ISO 32000-1 7.4

> **Started:** 2026-10-22T06:30:00Z

The audit of `D00 T03 §4` (`docs/reviews/06-quality/audit-d00-t03-s4.md`, D06 T05 §12) reached its 22-round cap (`stopped-at-22`). Its last four rounds each found one more stream decoder in `scripts/build-corpus.py`'s validators that was laxer than ISO 32000-1 7.4: the deflate bomb's outer stage (round 19), LZW code widths (round 20), the LZW initial clear code (round 21) and ASCII85 one-character final groups (round 22), each fixed in turn. The pattern, not one more case, is the leftover: this section reviews every validator decoder against 7.4 at once, so that a generator regression producing a malformed filter stream cannot pass the corpus checkpoint while `src/pdf/filters.cpp` would refuse the same bytes. -> XREF: D06 T05 §12

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. List every decoder the validators call (`inflate`, `inflate_bounded`, `flate_png_up_decompress`, `ascii85_decode_pdf`, the ASCIIHex path, `rle_decode_pdf`, `lzw_decode_pdf`, and the CCITT, JBIG2, DCT and JPX seed checks) with the 7.4 clause each implements. Done when: the list is in the section's findings file with one line per decoder.
2. Make each decoder refuse what its clause forbids, matching the refusals in `src/pdf/filters.cpp`. Done when: each has a test that fails against the tree before this section.

- [x] Write the decoder inventory with each decoder's ISO 32000-1 7.4 clause and the matching `src/pdf/filters.cpp` refusal into `docs/reviews/06-quality/D06-T05-s26.md`. Done when: every decoder the validators call appears with its clause.
- [x] Make the ASCIIHex path in `decode_seed_stream` and `validate_seed_filter_bin_structure` refuse non-hex characters other than whitespace, a missing `>` and data after it, per 7.4.2. Done when: a test with each malformation fails before the change and passes after.
- [x] Make `rle_decode_pdf` refuse a run that reads past the data, a missing EOD (128) and data after EOD, per 7.4.5. Done when: a test with each malformation fails before the change and passes after.
- [x] Make `flate_png_up_decompress` refuse a row whose predictor byte is not 2 (Up) and a length that is not whole rows, per 7.4.4.4. Done when: a test with each malformation fails before the change and passes after.
- [x] Replace the CCITT, JBIG2, DCT and JPX seed checks that compare against the generator's own output or only markers with checks of the structure 7.4.6, 7.4.7, 7.4.8 and 7.4.9 define (CCITT `/K`, `/Columns` and EOFB; JBIG2 segment headers; DCT SOI, SOF, SOS, EOI order; JPX box and codestream markers). Done when: a test regenerating each seed with one structural fault fails before the change and passes after.
- [x] Commit: `corpus: validator filter decoders conform to ISO 32000-1 7.4`

**Test checkpoint:** `python -m unittest tests.scripts.test_build_corpus` passes and `python scripts/build-corpus.py --check` prints `verified: 39 corpus fixtures`.

> **Verified:** 2026-10-22 | §26 | b41612ceff57e39cb9bb4c4aa8e5472f1978a526
> **Review:** `docs/reviews/d06-t05-s26.json`

## 27. ARM64 Debug build free of the CS1668 LIB search-path warnings

> **Started:** 2026-10-22T09:40:00Z

The audit of `D00 T03 §5` (`docs/reviews/06-quality/audit-d00-t03-s5.md`, D06 T05 §13) found the full `.\build.cmd` at `8d88cac` exiting 0 but logging 4 warnings while building `tests/recto-tests/recto-tests.vcxproj` for ARM64 Debug: `ROSLYNCODETASKFACTORYCSHARPCOMPILER : warning CS1668: Invalid search path` for `VC\Tools\MSVC\14.51.36231\atlmfc\lib\ARM64` and `Windows Kits\NETFXSDK\4.8\lib\um\arm64` on `LIB`. They come from MSBuild compiling an inline Roslyn code task under the ARM64 environment, not from Recto's C++, but `D00 T01 §3`'s checkpoint promises zero warnings on x64 and ARM64. -> XREF: D06 T05 §13

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Find the task that runs through `RoslynCodeTaskFactory` in the ARM64 Debug `recto-tests` build, through `build/build.ps1` (which sets up vcpkg) with a binary log. Done when: the task name, the file that declares it and the `LIB` entries it inherits are recorded in this section's findings file.
2. Fix it at the layer that owns it: the project or `Directory.Build.*` property that adds the absent ATL/MFC and .NET ARM64 library folders to `LIB`, or the declaring package's version. Done when: the warnings are gone without suppressing CS1668.

- [x] Capture a binary log of the ARM64 Debug `recto-tests` build through `build/build.ps1` (**Corrected 2026-10-22:** `MSBuildDebugEngine` binary logs and `-getProperty:LibraryPath` served; the warning reproduces only when vcpkg's install step runs, forced by back-dating its stamp) and name the inline task, its declaring `.targets` file and the `LIB` value it sees, in `docs/reviews/06-quality/D06-T05-s27.md`. Done when: the findings file names all three.
- [x] Remove the absent ARM64 folders from `LIB` at their source (a project property, `Directory.Build.props`, or an installed but unused component), or update the package that declares the task. Done when: `.\build.cmd` logs `0 Warning(s)` for every configuration and no warning is suppressed or filtered.
- [x] Add a check to `build/build.ps1` that fails the run when MSBuild reports any warning in any configuration. Done when: reintroducing an absent folder on `LIB` makes `.\build.cmd` exit non-zero.
- [x] Commit: `build: ARM64 Debug builds without CS1668 warnings`

**Test checkpoint:** `.\build.cmd` exits 0 and its log shows `0 Warning(s)` for x64 and ARM64, Debug and Release.

> **Verified:** 2026-10-22 | §27 | b86dcd3141aafd057e082c2fe50553f6f7623671
> **Review:** `docs/reviews/d06-t05-s27.json`

## 28. Retired Office and LibreOffice converter surfaces removed from the A, B and C boards

> **Started:** 2026-10-22T11:40:00Z

While removing MuPDF and AGPL text from the older mockup boards (`D06 T05 §25`), the A, B and C boards were found to still show the retired Office and LibreOffice dependency as whole surfaces: an "Office to PDF needs Microsoft Office or LibreOffice" dialog (`A-Dialogs`, `B-Dialogs`, `C-Dialogs`, with "Get LibreOffice" links) and an "Office to PDF converter" setting reporting which suite is installed (`A-Settings`, `C-Settings`). Recto converts Office files with its own built-in engines and has no Office or LibreOffice dependency (D5, D7, D10), so these surfaces describe a product that no longer exists. (`D06 T05 §25` already reworded the Office readiness banner and the waiting jobs on `A-Convert` and `B-Convert`; this section's grep covers every such phrasing so none returns.) Removing or replacing a dialog is a design choice the current `D-` boards and `design/SPEC.md` own, so it is its own section rather than a text swap. -> XREF: D06 T05 §25

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Decide each surface against `design/SPEC.md` and the `D-` boards: drop the dialog and the converter setting, or replace them with what the SPEC shows for Office import today. Done when: the choice and its SPEC section are recorded in this section's findings file.
2. Edit the boards and re-render them. Done when: `git grep -n -i -E "libreoffice|office com|microsoft (word|excel|powerpoint|office)|through (word|excel|powerpoint|office)" -- design/mockups` prints nothing.

- [x] Record, per surface, what `design/SPEC.md` and the `D-` boards show for Office import, and whether the old surface is dropped or replaced, in `docs/reviews/06-quality/D06-T05-s28.md`. Done when: each of the five boards has a recorded choice with its SPEC section.
- [x] Edit `design/mockups/source/[ABC]-Dialogs.dc.html` and `[AC]-Settings.dc.html` to that choice. Done when: `git grep -n -i -E "libreoffice|office com|microsoft (word|excel|powerpoint|office)|through (word|excel|powerpoint|office)" -- design/mockups/source` prints nothing.
- [x] Re-render those boards with `node design/tools/render-mockups.mjs --only <names>`. Done when: the same grep over `design/mockups/html` prints nothing and each board's PNG is newer than its source.
- [x] Extend `tests/scripts/test_engine_dependency_policy.py`'s design check to refuse Office or LibreOffice named as a converter dependency in `design/mockups/`. Done when: restoring one "Get LibreOffice" link makes the test fail.
- [x] Commit: `design: retire the Office and LibreOffice converter surfaces from the A, B and C boards`

**Test checkpoint:** `python -m unittest tests.scripts.test_engine_dependency_policy` passes, and `git grep -n -i -E "libreoffice|office com|microsoft (word|excel|powerpoint|office)|through (word|excel|powerpoint|office)" -- design/mockups` prints nothing.

> **Verified:** 2026-10-22 | §28 | 7cda56b0c6b8400a8bae026f61241f293342ed13
> **Review:** `docs/reviews/d06-t05-s28.json`

## 29. Damaged-file recovery resolves indirect object-stream parameters

> **Started:** 2026-10-23T08:20:00Z

The audit of `D01 T01 §6` (`docs/reviews/06-quality/audit-d01-t01-s6.md`, D06 T05 §21) found damaged-file recovery decoding each `/Type /ObjStm` it meets with no resolver (`src/pdf/xref.cpp`, the recovery scan's `decode_stream(stm)`), so a container whose `/Filter`, `/DecodeParms`, `/N` or `/First` is an indirect reference loses every object in it, including a sole `/Catalog`, and recovery fails. Normal resolution handles these since D06 T05 §20 and §21. Recovery is a single forward scan, so a parameter object can lie after the container that needs it: the fix defers object-stream unpacking until the scan has found every uncompressed object, which changes when compressed objects enter the rebuilt table and must keep the scan's rule that later definitions override earlier ones. -> XREF: D06 T05 §21

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Defer object-stream unpacking in the recovery scan of `XrefTable::parse` until the forward scan ends, keeping each container's file position so the override order stays the file order. Done when: a recovered table built from a file whose containers and uncompressed objects interleave matches the order the scan used before, proven by the existing recovery tests. **Corrected 2026-10-23:** only containers whose `/Filter`, `/DecodeParms`, `/N` or `/First` is or holds a reference are deferred (`objstm_needs_resolver` in `src/pdf/xref.cpp`); every other container is still unpacked where the scan meets it, because its integers can be the indirect `/Length` of a later stream (deferring all of them would lose that). The file order holds through `defined_at`, the file position of the definition each entry came from, and catalog candidates carry their position so the fallback still prefers the latest in the file.
2. Decode each deferred container with a resolver over the uncompressed objects the scan found (cycle-safe, one level) and read it through `parse_object_stream` and `extract_object_stream_member`. Done when: a damaged file whose only `/Catalog` sits in a container with `/DecodeParms 2 0 R` recovers.

- [x] Record each `/Type /ObjStm` the recovery scan meets with its file offset instead of unpacking it in place in `src/pdf/xref.cpp`. Done when: `PdfXref.*` recovery tests pass unchanged. **Corrected 2026-10-23:** recorded only when its parameters need resolving (see build order step 1); the others unpack in place as before.
- [x] Unpack the recorded containers after the scan in file order, with an `IndirectObjectResolver` over the scan's uncompressed entries, through `parse_object_stream` and `extract_object_stream_member`. Done when: a later uncompressed definition of an object still overrides an earlier compressed one, and the reverse, as before. **Corrected 2026-10-23:** the header is read by the scan's existing header reader, moved unchanged into `unpack_objstm` and shared by both paths, and members through `extract_object_stream_member`, so a deferred container is read exactly as one unpacked in place.
- [x] Keep the cumulative decoded `/ObjStm` byte budget and the per-container ceilings on the deferred path. Done when: `tests/corpus/broken/` files that hit the budget still fail with `DecompressionLimitExceeded`.
- [x] Add `PdfXref.RecoveryResolvesIndirectObjStmParameters`: a file with no `startxref` whose only `/Catalog` is compressed in an ASCIIHex container with `/DecodeParms 2 0 R` (object 2 after the container), and a variant with `/N 3 0 R /First 4 0 R`. Done when: both recover, and the test fails on the code before this section.
- [ ] Commit: `pdf: recovery resolves indirect object-stream parameters`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfXref.*` passes under ASan with 0 warnings, including `PdfXref.RecoveryResolvesIndirectObjStmParameters`.

## 30. Damaged-file recovery keeps an encrypted file's compressed objects

> **Started:** 2026-10-23T09:05:00Z

The audit of `D01 T01 §6` (`docs/reviews/06-quality/audit-d01-t01-s6.md`, D06 T05 §21, round 5) found damaged-file recovery losing every compressed object of an encrypted file. The recovery scan copies only the trailer keys of an intact `/Type /XRef` stream it meets and drops its type-2 rows, and it tries to decode each `/Type /ObjStm` it meets, which in an encrypted file is still encrypted because authentication happens after recovery (`Document` opens, recovers, then authenticates). So a file whose only damage is a wrong `startxref` offset, opened with the right password, resolves its compressed objects to null. Cross-reference streams are never encrypted (ISO 32000-1 section 7.5.8.1), so their rows are usable before authentication; containers are not. -> XREF: D06 T05 §21

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Import the type-2 rows of each intact cross-reference stream recovery meets, under the scan's rule that later definitions override earlier ones. Done when: a file with a wrong `startxref` and an intact xref stream recovers its compressed entries before any container is decoded.
2. Leave encrypted containers undecoded during recovery, and let resolution decode them after authentication through `SecurityHandler` and `Document` (`parse_object_stream`, `extract_object_stream_member`). Done when: the encrypted fixture below resolves its compressed objects after recovery and authentication.

- [x] Import type-2 rows from intact `/Type /XRef` streams in the recovery scan of `src/pdf/xref.cpp`, keeping file-order override against uncompressed definitions. Done when: `PdfXref.*` recovery tests pass unchanged.
- [x] Skip decoding `/Type /ObjStm` streams during recovery when the file has an `/Encrypt` entry in any recovered trailer, recording the containers for resolution after authentication. Done when: recovery of an encrypted file no longer decodes ciphertext as a container. **Corrected 2026-10-23:** the trailer usually follows the containers, so a single forward scan cannot know in time; a file that names `/Encrypt` anywhere defers every container (the `D06 T05 §29` path), and none is decoded once a recovered trailer has `/Encrypt`. A trailer `/Root` naming a compressed object an xref stream lists is accepted in an encrypted file, since its container cannot be read before authentication.
- [x] Add `PdfDocument.RecoveredEncryptedObjectStreamsResolve`: the fixture of `PdfDocument.EncryptedObjectStreamsShareOneDecodeAndRefuseForbiddenMembers` with its final `startxref` offset corrupted, opened with the password. Done when: objects 21 and 22 resolve to 7 and 8, and the test fails on the code before this section.
- [ ] Commit: `pdf: recovery keeps an encrypted file's compressed objects`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfXref.*:PdfDocument.*` passes under ASan with 0 warnings, including `PdfDocument.RecoveredEncryptedObjectStreamsResolve`.

## 31. JBIG2 striped pages, MMR halftone alignment and HENABLESKIP

> **Started:** 2026-10-23T10:30:00Z

The audit of `D01 T01 §8` (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, round 1) found three JBIG2 decoding gaps against ITU-T T.88. A page of unknown height (`0xFFFFFFFF`) with striping takes its maximum stripe size as the permanent height and ignores End-of-Stripe segments, so later stripes are clipped (T.88 7.4.9 and 7.4.10). Consecutive MMR halftone bitplanes are read from the exact bit where the previous one ended, though each MMR invocation begins and ends on a byte boundary (T.88 6.2.6); the existing fixture concatenates unaligned planes and hides this. Arithmetic halftone regions never read `HENABLESKIP` (flag bit `0x08`) and decode symbols for skipped cells (T.88 6.6.5, Annex C). Round 2 added a fourth: symbol dictionaries never read flag bits 8 and 9, so retained arithmetic contexts are reset instead of restored (T.88 7.4.2.2). -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Make page height follow End-of-Stripe segments for unknown-height pages in `src/pdf/filters.cpp`. Done when: a page striped into two one-row regions decodes both rows.
2. Byte-align each MMR halftone plane and implement the halftone skip mask. Done when: byte-aligned planes decode and skipped cells consume no symbols.

- [x] Track the page height of a `0xFFFFFFFF`-height page from End-of-Stripe segments (T.88 7.4.9, 7.4.10) in the JBIG2 page composer in `src/pdf/filters.cpp`. Done when: an 8-pixel page with striping `0x8001` and two one-row regions separated by End-of-Stripe decodes two rows.
- [x] Align to the next byte after each MMR halftone bitplane (T.88 6.2.6) and correct the `PdfFilters` halftone fixture to pad its first plane from 38 to 40 bits. Done when: the corrected fixture decodes and the unaligned form is refused. **Corrected 2026-10-23:** the unaligned form is not always refused (its second plane may still parse); the test asserts it does not decode to the aligned result.
- [x] Read `HENABLESKIP` and build the halftone skip mask (T.88 6.6.5, Annex C) for arithmetic halftones in `src/pdf/filters.cpp`. Done when: a grid with cells outside the region and `HENABLESKIP=1` decodes with those cells skipped.
- [x] Honour symbol dictionary flag bits 8 and 9 (bitmap coding context used and retained, T.88 7.4.2.2): restore the referenced dictionary's generic and refinement contexts and keep this dictionary's when asked, in `src/pdf/filters.cpp`. Done when: a dictionary encoded with the contexts another retained decodes correctly.
- [x] Add `PdfFilters.Jbig2StripesAlignmentAndSkip` covering all three. Done when: it fails on the code before this section.
- [ ] Commit: `pdf: JBIG2 stripes, aligned MMR halftone planes and HENABLESKIP`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.Jbig2StripesAlignmentAndSkip`.

## 32. JPX per-component precision and sampling, and the full coding-style set

The audit of `D01 T01 §8` (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, round 1) found the native JPEG 2000 decoder taking every component's bit depth and signedness from component zero and ignoring `XRsiz`/`YRsiz` subsampling (ITU-T T.800 A.5.1), and rejecting more than 10 decomposition levels and every nonzero code-block style, while `D01 T01 §8`'s original review recorded 32 levels and all six style flags as supported. Round 2 added region-of-interest scaling: `RGN` markers are skipped and coefficients are not realigned (T.800 A.6.3, Annex H). -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Carry `Ssiz`, `XRsiz` and `YRsiz` per component through decoding and reconstruction in `src/pdf/filters.cpp`. Done when: components of different precision and 2x2 chroma subsampling reconstruct correctly.
2. Support up to 32 decomposition levels and the T.800 code-block style flags (selective arithmetic bypass, reset, termination, vertically causal context, predictable termination, segmentation symbols). Done when: a codestream using each decodes.

- [ ] Read precision and signedness per component from SIZ and normalize each component by its own depth in the JPX reconstruction in `src/pdf/filters.cpp`. Done when: a codestream with components of 8 and 12 bits decodes each to its own range.
- [ ] Apply `XRsiz`/`YRsiz` to component bounds and upsample subsampled components to the image grid. Done when: a 4:2:0 codestream reconstructs at full resolution.
- [ ] Accept up to 32 decomposition levels and implement the six code-block style flags in the Tier-1 decoder. Done when: `seed-filter-09-jpx.bin` with byte 58 set to `0x02` (context reset) decodes, and each other flag has a decoding test.
- [ ] Read `RGN` (`0xFF5E`) markers and apply the region-of-interest Maxshift realignment to coefficients (T.800 A.6.3, Annex H) in the native decoder in `src/pdf/filters.cpp`. Done when: a lossless grayscale codestream encoded with `-ROI c=0,U=4` decodes to its original samples.
- [ ] Keep the JP2 header's colour (`colr`, enumerated and restricted ICC), palette (`pclr`, `cmap`) and channel definitions (`cdef`) in the native container reader in `src/pdf/filters.cpp`, expand palette indices, and return CMYK as four colour channels and an alpha only where `cdef` declares one, for both the native and the WIC path (ISO 32000-1 8.9.5.3, T.800 Annex I). Added 2026-10-23 from the `D06 T05 §22` completion review (gap 6). Done when: a palette image keeps its red, a four-component CMYK JP2 stays four colour channels, and a declared opacity channel becomes alpha.
- [ ] Add `PdfFilters.JpxComponentsAndCodingStyles` for the cases above, and record in `docs/reviews/06-quality/audit-d01-t01-s8.md` that the original review's capability claims were corrected here. Done when: the test fails on the code before this section.
- [ ] Commit: `pdf: JPX per-component geometry and the full coding-style set`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.JpxComponentsAndCodingStyles`.

## 33. DCT /ColorTransform and Adobe APP14 CMYK and YCCK decoding

The audit of `D01 T01 §8` (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, round 1) found DCT decoding ignoring `/ColorTransform` (WIC's interpretation is always used), so a three-component JPEG without an Adobe marker and `/ColorTransform 0` is colour-converted when it must not be (PDF Reference Table 3.11). It also found that the checked Adobe CMYK/YCCK verification of `D01 T01 §8` never existed: the generated JPEG has no APP14 marker, its samples are neutral and no YCCK case is tested. -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Honour `/ColorTransform` (and the Adobe APP14 transform flag when present) when decoding through WIC in `src/pdf/filters.cpp`. Done when: `/ColorTransform 0` yields untransformed samples.
2. Test Adobe APP14 CMYK and YCCK images with non-neutral samples. Done when: both decode to the expected colours.

- [ ] Read `/ColorTransform` in the DCT decoder and decode with or without the YCbCr/YCCK transform accordingly, preferring the Adobe APP14 transform byte when present, in `src/pdf/filters.cpp`. Done when: a non-neutral three-component JPEG with `/ColorTransform 0` decodes to its raw samples.
- [ ] Add JPEG fixtures with an Adobe APP14 marker for CMYK (transform 0) and YCCK (transform 2) with non-neutral samples under `tests/corpus/` with provenance rows. Done when: `python scripts/build-corpus.py --check` passes with them.
- [ ] Add `PdfFilters.DctColorTransformAndAdobeCmykYcck` asserting the decoded colours. Done when: it fails on the code before this section.
- [ ] Commit: `pdf: DCT /ColorTransform and Adobe CMYK and YCCK`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.DctColorTransformAndAdobeCmykYcck`.

## 34. CCITT damaged-row recovery as ISO 32000-1 Table 11 specifies

The audit of `D01 T01 §8` (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, round 1) found CCITT damaged-row handling diverging from `/DamagedRowsBeforeError` (ISO 32000-1 Table 11): consecutive damaged rows each repeat the last recovered row (a second consecutive substitution should be white), and mixed Group 3 (`K > 0`) decoding has no recovery and refuses the stream. -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement Table 11's damaged-row substitution for pure and mixed Group 3 in `src/pdf/filters.cpp`. Done when: two consecutive damaged rows after a non-white good row produce the good row then white, for `K = 0` and `K > 0`.

- [ ] Resynchronize at the next EOL after a damaged row and substitute per Table 11 (the previous good row once, then white) up to `/DamagedRowsBeforeError`, in the CCITT decoder in `src/pdf/filters.cpp`. Done when: the `K = 0` case yields the good row followed by a white row.
- [ ] Apply the same recovery to mixed Group 3 (`K > 0`), honouring each row's 1D/2D tag bit. Done when: the `K > 0` case recovers instead of being refused.
- [ ] Add `PdfFilters.CcittDamagedRowRecovery` for both cases and for exceeding `/DamagedRowsBeforeError`. Done when: it fails on the code before this section.
- [ ] Commit: `pdf: CCITT damaged-row recovery per ISO 32000-1 Table 11`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.CcittDamagedRowRecovery`.

## 35. JBIG2 decoder work and memory bounded at every allocation and loop

The audit of `D01 T01 §8` (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22) found a new JBIG2 resource gap in each of rounds 2 and 3: retained symbols charged only their pixel bytes, MMR transition buffers unbounded, text-region symbol references uncharged, and halftone compositing walking patterns entirely outside the page. Each was fixed where found, but one site per round is the pattern this section ends: a systematic pass over every allocation and every data-driven loop in the JBIG2 decoder, the way `D06 T05 §26` covered the corpus validator's decoders. It also owns the regression fixtures for three of those round-3 fixes, which shipped without a test that fails before them: text-region reference charging, compositing clipped to the destination, and unsigned `HRX`/`HRY` (T.88 7.4.5.1, Table 20). -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Inventory every allocation and every loop whose trip count comes from segment data in the JBIG2 decoder in `src/pdf/filters.cpp`, and record for each the budget or bound that holds it. Done when: the inventory is in this section's findings file with no entry unbounded.
2. Add the missing bounds and the three owed regression fixtures. Done when: each fixture fails with its fix reverted.

- [ ] Write the inventory (allocation or loop, `file:line`, the bound that holds it) for the JBIG2 decoder in `docs/reviews/06-quality/D06-T05-s35.md`. Done when: every entry names a budget charge (`state.account_bytes`, `jbig2_footprint`, the decode work meter) or a structural bound.
- [ ] Bound each entry the inventory finds unbounded, at its owning site in `src/pdf/filters.cpp`. Done when: the inventory has no unbounded entry.
- [ ] Add a fixture for text-region symbol references: a dictionary of many symbols listed many times by one text region at a small ceiling. Done when: it is refused with `DecompressionLimitExceeded`, and accepted with the reference charging reverted (so it fails then).
- [ ] Add a fixture for compositing far outside the destination: a halftone grid of large patterns placed outside a small region. Done when: it decodes within a time bound, and the unclipped loop exceeds it.
- [ ] Add a fixture for unsigned `HRX`/`HRY`: two 1x1 patterns, a 129x1 region, a 2x1 grid, `HRX=0x8000`. Done when: the second pattern lands at x=128, and the signed reading fails it.
- [ ] Commit: `pdf: JBIG2 decoder bounded at every allocation and loop`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including the three fixtures above, and `docs/reviews/06-quality/D06-T05-s35.md` lists no unbounded entry.

## 36. Native JPX decoder bounded by a cumulative work budget

The completion review of the `D01 T01 §8` audit (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, gap 8) found that the native JPEG 2000 decoder bounds its memory but not its work: packet, code-block and Tier-1 loops iterate layers, resolutions, components and precincts with no cumulative budget, so a small damaged or hostile codestream can keep a decode thread busy far past any useful time. -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Charge a cumulative work budget in the native JPX decoder in `src/pdf/filters.cpp`. Done when: every data-driven loop charges it.
2. Prove the refusal with a fixture. Done when: the fixture is refused and fails with the charge removed.

- [ ] Inventory every loop in the native JPX decoder whose trip count comes from codestream data, in `docs/reviews/06-quality/D06-T05-s36.md`, with the charge or structural bound that holds each. Done when: no entry is unbounded.
- [ ] Charge each such loop to one per-decode work budget derived from the decode ceiling, refusing with `ErrorCode::ResourceLimit` when it runs out, in `src/pdf/filters.cpp`. Done when: the inventory names the charge for every entry.
- [ ] Add `PdfFilters.JpxWorkBudget`: a small codestream with many layers and empty packets that costs far more work than its size. Done when: it is refused with `ResourceLimit` within one second, and decodes (slowly) with the charge removed, so the test fails then.
- [ ] Commit: `pdf: native JPX decoder bounded by a work budget`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.JpxWorkBudget`.

## 37. WIC decode failures classified by HRESULT, not reported as damage

> **Started:** 2026-10-23T13:00Z

The completion review of the `D01 T01 §8` audit (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, gap 10) found that every WIC failure in the DCT and JPX paths becomes `DamagedFormat` without reading the HRESULT, so an `E_OUTOFMEMORY` is reported as file corruption and a JPX resource failure falls through to the native decoder. A user then sees "damaged" for a file that is fine. -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Classify WIC HRESULTs in one helper in `src/pdf/filters.cpp`. Done when: every WIC call site uses it.
2. Prove the classes with injected failures. Done when: each class has a test.

- [x] Add one helper in `src/pdf/filters.cpp` that maps a WIC HRESULT to `OutOfMemory` (`E_OUTOFMEMORY`), `UnsupportedFeature` (`WINCODEC_ERR_COMPONENTNOTFOUND`, `WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT`), `Cancelled` (`E_ABORT`) or `DamagedFormat` (data errors such as `WINCODEC_ERR_BADIMAGE`), with the HRESULT in hex in the message. Done when: every WIC failure path in the DCT and JPX decoders returns through it.
- [x] Fall back from WIC to the native JPX decoder only for codec-absent and data-format failures, never after a resource failure. Done when: an injected `E_OUTOFMEMORY` from WIC returns `OutOfMemory` without a native attempt.
- [x] Add a test-only HRESULT injection point for the WIC calls and `PdfFilters.WicFailureClasses` covering each class. Done when: the test fails on the code before this section.
- [ ] Commit: `pdf: WIC failures classified by HRESULT`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.WicFailureClasses`.

## 38. DCT decodes only the pixel representation the caller asked for

> **Started:** 2026-10-23T13:39Z

The completion review of the `D01 T01 §8` audit (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, gap 15) found that a DCT decode keeps the raw samples, an RGBA copy and a BGRA copy at once: 11 bytes per pixel for RGB. A 6000 x 4500 camera photo needs 297,000,000 bytes and is refused under the default 256 MiB ceiling, although one display buffer of 108,000,000 bytes fits. -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Let the caller name the representation it needs in `decode_dct_image` in `src/pdf/filters.h` and `src/pdf/filters.cpp`. Done when: only that buffer is allocated.
2. Prove it on a large photo. Done when: the photo decodes within the default ceiling.

- [x] Add the requested representation (raw samples, RGBA or BGRA) to the DCT decode parameters and allocate only that buffer in `src/pdf/filters.cpp`. Done when: a decode allocates one image-sized buffer.
- [x] Charge the ceiling with the bytes of the requested representation only. Done when: the 6000 x 4500 RGB case is not refused.
- [x] Add `PdfFilters.DctLargePhotoWithinCeiling`: a generated 6000 x 4500 baseline JPEG decoded at the default ceiling, with `test::ScopedHeapMeter` showing a peak under one buffer plus 10 percent. Done when: the test fails on the code before this section.
- [ ] Commit: `pdf: DCT decodes only the requested pixel representation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.DctLargePhotoWithinCeiling`.

## 39. Per-codec decode latency measured on valid, large and damaged fixtures

The completion review of the `D01 T01 §8` audit (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22, gap 16) found no measurement behind any speed claim for the image decoders: the one timing assertion only catches quadratic CCITT behaviour. Product ambition (`AGENTS.md`) asks for measured claims, not adjectives. -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add a decoder benchmark over fixed fixtures. Done when: it prints cold and warm p50 and p95 per codec.
2. Record the baseline. Done when: the numbers are committed with the machine they ran on.

- [ ] Add `PdfFiltersBench.*` to `tests/recto-tests/` (disabled by default, run explicitly) timing CCITT, JBIG2, DCT and JPX decodes on one valid, one large and one damaged fixture each from `tests/corpus/`, reporting cold and warm p50 and p95. Done when: the benchmark prints a table of twelve rows.
- [ ] Record the baseline on the reference machine `D00 T01 §5` names, in `docs/benchmarks/decoders.md`, with the build configuration and date. Done when: the file holds the table and a rerun lands within 10 percent of it.
- [ ] Commit: `bench: per-codec decode latency baseline`

**Test checkpoint:** `recto-tests.exe --gtest_also_run_disabled_tests --gtest_filter=PdfFiltersBench.*` prints twelve rows and `docs/benchmarks/decoders.md` holds the recorded baseline.

## 40. Image decoder allocations drawn from one budgeted memory resource per decode

The audit of `D01 T01 §8` (`docs/reviews/06-quality/audit-d01-t01-s8.md`, D06 T05 §22) found a memory-accounting gap in the image decoders in each of rounds 4, 6 and 7: CCITT transitions charged apart from the output, then the output's reallocation peak; JPX tag trees, then tile coding overrides, then their quantization tables; parameter values copied per stage. Each was fixed where found by estimating an allocation and charging the estimate, and each round found the next estimate that was missing or short. As `D06 T05 §35` does for JBIG2's site inventory, this section ends the site-by-site pattern at the owning layer: every allocation a decode makes is drawn from one `std::pmr::memory_resource` that counts bytes against the decode's ceiling and refuses past it, so the ceiling holds by construction and no estimate can be missing. -> XREF: D06 T05 §22

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add a budgeted upstream resource and route the image decoders' containers through it in `src/pdf/filters.cpp`. Done when: CCITT, JBIG2, DCT (Recto's buffers; WIC's own are outside), JPX and the filter pipeline allocate through it.
2. Prove the ceiling holds by measurement. Done when: the tests below pass and fail with the resource bypassed.

- [ ] Add `detail::DecodeBudgetResource` (a `std::pmr::memory_resource` over the default resource that counts live bytes, records the peak, and throws `std::bad_alloc` past the decode's ceiling) and a scoped installer for the decode thread, in `src/pdf/filters.h` and `src/pdf/filters.cpp`. Done when: a unit test allocates past a 1 MiB budget and sees the refusal, and the peak it reports matches what was allocated.
- [ ] Convert the image decoders' working containers (`std::vector`, `std::deque`, the JPX and JBIG2 state structs) to `std::pmr` types bound to that resource, and map its refusal to `ErrorCode::DecompressionLimitExceeded` at each decoder's existing `std::bad_alloc` boundary. Done when: no image decoder allocates through the global heap during a decode (checked with `test::ScopedHeapMeter` outside ASan: under 64 KiB of global allocation for a decode that uses megabytes).
- [ ] Keep the existing per-site estimates only as early refusals and say so beside each; the resource is the ceiling. Done when: each estimate's comment names the resource as the bound.
- [ ] Add `PdfFilters.DecodeBudgetHoldsByConstruction`: the round 4, 6 and 7 reproductions in `docs/reviews/06-quality/audit-d01-t01-s8.md` decoded at their ceilings, each refused or within the ceiling by the resource's own peak. Done when: the test fails with the decoders on the global heap.
- [ ] Commit: `pdf: image decoders allocate from one budgeted memory resource`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.DecodeBudgetHoldsByConstruction`, and in x64 Debug the global-heap check of the second item holds.

## 41. Recovery resolves compressed parameter targets and an indirect container /Type

The completion review of `D06 T05 §29` (`docs/reviews/06-quality/D06-T05-s29.md`, gap 6) found two layouts damaged-file recovery still loses: a container whose `/Type` is an indirect reference to `/ObjStm`, and a container whose parameter (`/DecodeParms`, `/N`, `/First`) is itself compressed in another recoverable container. `§29` resolves parameters only through uncompressed objects, one level. -> XREF: D06 T05 §29

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Recognise an indirect container `/Type` once the scan has found every object, as the catalog check does with `catalog_names`. Done when: such a container is unpacked.
2. Resolve a parameter through a member of another container, decoding containers in dependency order with a cycle guard, inside the existing budgets. Done when: an acyclic two-container chain recovers and a cycle is refused.

- [ ] Record streams whose `/Type` is a reference in the recovery scan of `src/pdf/xref.cpp`, and treat them as containers when the named object is the name `/ObjStm`. Done when: a container with `/Type 9 0 R` (object 9 is `/ObjStm`, after it) recovers its members.
- [ ] Let the deferred-container resolver answer a reference to a compressed object from a container already unpacked, ordering the deferred containers by their dependencies and refusing a cycle, in `src/pdf/xref.cpp`. Done when: container A whose `/DecodeParms` is a member of container B recovers, and A and B naming each other leave both out without failing recovery.
- [ ] Add `PdfXref.RecoveryResolvesCompressedParameters` covering both cases and the cycle. Done when: the test fails on the code before this section.
- [ ] Commit: `pdf: recovery resolves compressed parameters and indirect container types`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfXref.*` passes under ASan with 0 warnings, including `PdfXref.RecoveryResolvesCompressedParameters`.

## 42. Cancellable damaged-file recovery with progress

The completion review of `D06 T05 §29` (`docs/reviews/06-quality/D06-T05-s29.md`, gap 9 and Part 2 shortfall 6) found that `XrefTable::recover_damaged_xref` has no cancellation or progress: its budgets bound the work, but a user opening a large damaged file cannot stop it. -> XREF: D06 T05 §29

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Thread a `CancelToken` and a progress callback through recovery in `src/pdf/xref.h` and `src/pdf/xref.cpp`. Done when: the scan, the deferred pass and decoding poll it.
2. Prove the response time. Done when: cancellation is acknowledged within 100 ms on the reference machine.

- [ ] Add an optional `CancelToken` (the engine's existing type) and a progress callback (bytes scanned of total) to `recover_damaged_xref` and `XrefTable::parse`, polled at least every 64 KiB scanned, per deferred container and between decode stages, returning `ErrorCode::Cancelled`. Done when: no partially built table escapes a cancelled call.
- [ ] Add `PdfXref.RecoveryIsCancellable`: a large generated damaged file cancelled from another thread during the scan and during the deferred pass. Done when: each returns `Cancelled` within 100 ms, and the test fails with the polls removed.
- [ ] Commit: `pdf: cancellable damaged-file recovery with progress`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfXref.*` passes under ASan with 0 warnings, including `PdfXref.RecoveryIsCancellable`.

## 43. Repaired copy of a recovered file, and salvage of a damaged page tree

The completion review of `D06 T05 §29` (`docs/reviews/06-quality/D06-T05-s29.md`, gap 10 and Part 2 shortfall 5) found that recovery rebuilds the object table only: a damaged page tree is not salvaged, and nothing writes a repaired copy that reopens without recovery. Acrobat repairs on open and offers to save the repaired file. -> XREF: D06 T05 §29

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Salvage pages from a damaged page tree in `src/pdf/document.cpp`. Done when: page objects the tree no longer reaches are found and ordered.
2. Write a repaired copy through the existing writer, temp-file-then-replace, original untouched. Done when: the copy reopens without recovery.

- [ ] When the page tree from the recovered catalog is broken (a missing `/Kids` target, a cycle, a wrong `/Count`), collect the `/Type /Page` objects the scan found, in file order, as the document's pages, recording the salvage as a recovered-error (`D06 T03 §2`). Done when: a file with a broken `/Pages` node opens with every surviving page.
- [ ] Add `Document::save_repaired_copy(path)` writing a full rewrite with a fresh cross-reference table through the writer's temp-file-then-replace path. Done when: the copy reopens with `recovered() == false`, its pages render the same, and the original file's bytes are unchanged.
- [ ] Add `PdfDocument.SalvagedPagesAndRepairedCopy` covering both, with a failed write leaving no partial file. Done when: the test fails on the code before this section.
- [ ] Commit: `pdf: salvage damaged page trees and save a repaired copy`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfDocument.*` passes under ASan with 0 warnings, including `PdfDocument.SalvagedPagesAndRepairedCopy`.

## 44. Damaged-file recovery measured against competitors on one corpus

The completion review of `D06 T05 §29` (`docs/reviews/06-quality/D06-T05-s29.md`, Part 2 shortfalls 1 and 2) found no measurement behind a recovery claim: which damaged files Recto recovers that Acrobat, Foxit, PDFelement and UPDF do not, and how fast. Product ambition (`AGENTS.md`) asks for measured claims. Running the competitors needs their installed products, so the comparison waits on the owner. -> XREF: D06 T05 §29

**Phase:** 1
**Needs:** windows-host, owner-decision
**Surface:** no

**Build order:**

1. Assemble a damaged corpus and measure Recto. Done when: files recovered, pages kept, median and p95 time and peak memory are recorded.
2. Run the competitors on the same files. Done when: a table compares them with versions named.

- [ ] Add `tests/corpus/damaged/` (generated by `scripts/build-corpus.py`, with provenance rows): wrong `startxref`, truncated tails, broken xref streams, indirect container parameters, encrypted files with damaged xref, broken page trees. Done when: `Corpus.Provenance` covers every file.
- [ ] Record Recto's results (recovered, pages kept, median and p95 time, peak working set) on the reference machine in `docs/benchmarks/recovery.md`. Done when: the table is committed with the machine and build named.
- [ ] Owner decision: which competitor versions to install for the comparison. Done when: recorded here and the Need removed.
- [ ] Record each competitor's outcome on the same files beside Recto's. Done when: the table names every product and version.
- [ ] Commit: `bench: damaged-file recovery against competitors`

**Test checkpoint:** `docs/benchmarks/recovery.md` holds Recto's and each named competitor's results on every file in `tests/corpus/damaged/`.

## 45. Encrypted recovery without an intact xref stream: containers read after authentication

The completion review of `D06 T05 §30` (`docs/reviews/06-quality/D06-T05-s30.md`, gap 10) found that an encrypted file whose cross-reference streams are themselves damaged still loses its compressed objects: `§30` takes their locations from intact xref stream rows, and without them nothing says which members a container holds until it is decrypted, which happens after recovery. -> XREF: D06 T05 §30

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Keep the deferred containers' positions in the recovered table for an encrypted file. Done when: the table carries them past recovery.
2. Unpack them once authentication succeeds, through `SecurityHandler` and the recovery's member rules. Done when: the fixture below resolves its compressed objects.

- [ ] Record the positions of the containers an encrypted recovery left undecoded in the recovered `XrefTable` in `src/pdf/xref.cpp`, with the file positions their later-wins rule needs. Done when: `XrefTable` exposes them only for a recovered table.
- [ ] After `Document` authenticates a recovered encrypted file, decrypt and unpack those containers in file order with `parse_object_stream`, adding members only where no later definition exists, in `src/pdf/document.cpp`. Done when: objects compressed in them resolve.
- [ ] Add `PdfDocument.RecoveredEncryptedWithoutXrefStream`: the `encrypted_objstm_update` fixture with its xref stream's data damaged and `startxref` wrong, opened with the password. Done when: objects 21 and 22 resolve to 7 and 8, a wrong password still fails, and the test fails on the code before this section.
- [ ] Commit: `pdf: encrypted recovery reads its containers after authentication`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfDocument.*:PdfXref.*` passes under ASan with 0 warnings, including `PdfDocument.RecoveredEncryptedWithoutXrefStream`.

## 46. Attachment-only encryption: public pages open, `/EFOpen` asks only for attachments

The completion review of the `D01 T01 §9` audit (`docs/reviews/06-quality/audit-d01-t01-s9.md`, D06 T05 §23, gap 2) found that the Standard handler never reads a crypt filter's `/AuthEvent`: a document whose only encrypted content is its embedded files (`/AuthEvent /EFOpen`, ISO 32000-1 7.6.5) is treated as fully encrypted, so its public pages need the attachment password. The attachment prompt itself belongs to `D02 T03 §8`. -> XREF: D06 T05 §23

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Read `/AuthEvent` per crypt filter in `src/pdf/crypto.cpp`. Done when: the handler knows which content needs authentication.
2. Open the public content without a password and authenticate only for embedded files. Done when: the fixture below opens and its attachment needs the password.

- [ ] Record each crypt filter's `/AuthEvent` (`/DocOpen` default, `/EFOpen`) in `EncryptDictParams` and report whether opening needs a password. Done when: `PdfCrypto.ParseEncryptDict` covers both values.
- [ ] Let a document whose `/StmF` and `/StrF` are Identity and whose `/EFF` filter is `/EFOpen` open without authentication, decrypt nothing but embedded file streams, and return `NeedsPassword` for an embedded file until authenticated. Done when: pages and strings read unchanged and the attachment refuses, then decrypts after `authenticate`.
- [ ] Add an attachment-only fixture generated by `scripts/build-corpus.py` (with its provenance row) and `PdfDocument.AttachmentOnlyEncryption`. Done when: the test fails on the code before this section.
- [ ] Commit: `pdf: attachment-only encryption opens public content`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCrypto.*:PdfDocument.AttachmentOnlyEncryption` passes under ASan with 0 warnings.

## 47. Standard security handler interoperability matrix and cancellable decryption

The completion review of the `D01 T01 §9` audit (`docs/reviews/06-quality/audit-d01-t01-s9.md`, D06 T05 §23, gaps 4, 10, 11 and 12) found the full-file coverage limited to R=3, R=4 and R=6, no independently generated R=2 or R=5 file, no measured decryption cost, and no way to cancel the decryption of a large stream. -> XREF: D06 T05 §23

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Generate the missing fixtures and assert their plaintext. Done when: every revision opens with its user and owner passwords.
2. Add cancellation and a benchmark. Done when: a cancelled decryption returns within 100 ms and the numbers are recorded.

- [ ] Add R=2 (40-bit RC4) and R=5 (AES-256) files to `scripts/build-corpus.py` with provenance rows, and assert user and owner opening, plaintext and permissions for R=2 to R=6 in `PdfCrypto.InteroperabilityMatrix`, including `/EncryptMetadata false` and an AES stream with corrupted padding. Done when: every case passes and each fails with its revision's code path disabled.
- [ ] Accept an optional `CancelToken` in `SecurityHandler::decrypt_bytes` and the recursive object decryption, polled every 1 MiB and per object, returning `Cancelled` with nothing cached. Done when: `PdfCrypto.DecryptionIsCancellable` cancels a 64 MiB stream within 100 ms and a retry succeeds.
- [ ] Record correct, wrong and empty password authentication times and large-stream decryption throughput (median, p95, peak memory) on the reference machine in `docs/benchmarks/crypto.md`. Done when: the file holds the table with the build named.
- [ ] Commit: `pdf: security handler interoperability matrix and cancellable decryption`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCrypto.*` passes under ASan with 0 warnings, including `PdfCrypto.InteroperabilityMatrix` and `PdfCrypto.DecryptionIsCancellable`.

## 48. JBIG2 interoperability fixtures from an independent encoder, Huffman retained contexts

The completion review of `D06 T05 §31` (`docs/reviews/06-quality/D06-T05-s31.md`, gaps 1, 10 and 11) found two things the section's synthetic fixtures cannot settle. Retained contexts are honoured only for arithmetic symbol dictionaries: a Huffman dictionary with refinement aggregation (`SDHUFF=1`, `SDREFAGG=1`) neither restores nor keeps its refinement statistics (T.88 7.4.2.1.1, 7.4.2.2). And every JBIG2 fixture was hand-built by the same code that reads it, so no file from an independent encoder shows the striping, MMR halftone, skip-mask and retained-context paths decoding real archive scans. Building a Huffman refinement dictionary by hand is the job of an encoder, so this section brings one in as a test-data tool. -> XREF: D06 T05 §31

**Phase:** 1
**Needs:** windows-host, third-party
**Surface:** no

**Build order:**

1. Generate JBIG2 test streams with an independent encoder and record their provenance. Done when: the corpus has the files and their expected bitmaps.
2. Honour retained refinement contexts in Huffman dictionaries and decode the corpus exactly. Done when: every file matches its expected bitmap.

- [ ] Third party: choose an independent JBIG2 encoder usable as a test-data tool under a licence compatible with `GPL-3.0-or-later` corpus data (for example `jbig2enc`, or a reference T.88 encoder), and record the choice here. Done when: recorded and the Need removed.
- [ ] Generate, embed in PDFs and add to `tests/corpus/jbig2/` (with `Corpus.Provenance` rows and the encoder's version and command lines): a striped page of unknown height, MMR and arithmetic halftones with `HENABLESKIP`, arithmetic and Huffman symbol dictionaries with retained contexts (with and without refinement aggregation), and their expected bitmaps from the encoder's own decoder. Done when: `Corpus.Provenance` covers every file.
- [ ] Restore and retain the refinement contexts of Huffman dictionaries with refinement aggregation in `src/pdf/filters.cpp`, under the same last-referred-dictionary and coding-parameter checks as the arithmetic path. Done when: the Huffman fixtures decode exactly.
- [ ] Add `PdfFilters.Jbig2IndependentCorpus` comparing every file with its expected bitmap. Done when: it passes and fails with the Huffman change reverted.
- [ ] Commit: `pdf: JBIG2 decodes an independent encoder's corpus, Huffman retained contexts`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan with 0 warnings, including `PdfFilters.Jbig2IndependentCorpus`.

## Verification

- Every row above is `[x]`, each with an audit findings file under `docs/reviews/06-quality/`.
- `python scripts/todo-graph.py query rounds` shows no stamped section whose only reviews predate this file without an audit row here.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
