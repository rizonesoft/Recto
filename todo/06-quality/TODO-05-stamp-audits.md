---
schema_version: 1
id: stamp-audits
domain: 06-quality
status: active
title: "TODO-05 -- Audits of Sections Stamped Before the Current Review Process"
---

# TODO-05 -- Audits of Sections Stamped Before the Current Review Process

> **Goal:** Every section stamped before 2026-10-21 is re-verified under the current review process before later work builds further on it: its own Test checkpoint re-run at the current tip, and fresh independent audit rounds by the pinned reviewer with a 22-round cap and the severity cutoff. A confirmed stamp stays; a regression flips its row back with a dated blocking note, a filed repair and a list of suspect downstream stamps.

> **Current state:** Measured 2026-10-21. 24 sections carry stamps from the earlier process (`python scripts/todo-graph.py query rounds` lists them with `round_cap` null): 14 in `00-workspace` and 10 in `01-engine`. They were reviewed under a three-round cap by a different reviewer; the operator asked on 2026-10-21 for them to be brought up to the current standard.

## Inputs

- `.claude/skills/review-todo-section/SKILL.md` (audit stance, the 22-round cap and the severity cutoff) and `docs/review-round-prompt.md` (severity tags).
- Each audited section's own contract and receipt (`python scripts/todo-graph.py resolve "<ref>"`, `docs/reviews/<ref>.json`).
- The run file `docs/phase-runs/2026-10-21-phase-0.md` (the operator's request).

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
| 15 | §15 | Audit D01 T01 §1: PdfResult<T>, PdfError, ErrorCode, D16 diagnostic ring and ETW | D01 T01 §1, §14 | [ ] |
| 16 | §16 | Audit D01 T01 §2: PdfValue variant object model, dictionary/array helpers and deep | D01 T01 §2, §15 | [ ] |
| 17 | §17 | Audit D01 T01 §3: Binary-safe PDF 1.7 and 2.0 lexer (src/pdf/lexer.cpp) | D01 T01 §3, §16 | [ ] |
| 18 | §18 | Audit D01 T01 §4: Bounded recursive-descent indirect object parser (src/pdf/parser.cpp) | D01 T01 §4, §17 | [ ] |
| 19 | §19 | Audit D01 T01 §5: Classic xref tables, trailer merging and /Prev incremental update | D01 T01 §5, §18 | [ ] |
| 20 | §20 | Audit D01 T01 §7: Lossless stream filters (Flate, LZW, predictors, ASCII85, ASCIIHex | D01 T01 §7, §19 | [ ] |
| 21 | §21 | Audit D01 T01 §6: PDF 1.5+ /Type /XRef streams, /Type /ObjStm streams and damaged-file | D01 T01 §6, §20 | [ ] |
| 22 | §22 | Audit D01 T01 §8: Bi-level and image stream filters (CCITTFaxDecode, JBIG2Decode, WIC | D01 T01 §8, §21 | [ ] |
| 23 | §23 | Audit D01 T01 §9: Standard Security Handler decryption (RC4, AES-128, AES-256 R=2..6 | D01 T01 §9, §22 | [ ] |
| 24 | §24 | Audit D01 T01 §10: pdf::Document page-tree resolver, metadata, /Outlines, font list and | D01 T01 §10, §23 | [ ] |
| 25 | §25 | Retired MuPDF and AGPL text removed from the A, B and C mockup boards | §2 | [x] |
| 26 | §26 | Corpus validator filter decoders conform to ISO 32000-1 7.4 | §12 | [x] |
| 27 | §27 | ARM64 Debug build free of the CS1668 LIB search-path warnings | §13 | [x] |
| 28 | §28 | Retired Office and LibreOffice converter surfaces removed from the A, B and C boards | §25 | [x] |

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
- [ ] Commit: `review: audit D01 T01 §1 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §1"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s1.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 16. Audit D01 T01 §2: PdfValue variant object model, dictionary/array helpers and deep

`D01 T01 §2` (`PdfValue` variant object model, dictionary/array helpers and deep cloning) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s2.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §2`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s2.md`.
2. Audit `D01 T01 §2` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s2.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §2"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s2.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §2`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s2.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s2.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §2`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s2.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §2 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §2"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s2.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 17. Audit D01 T01 §3: Binary-safe PDF 1.7 and 2.0 lexer (src/pdf/lexer.cpp)

`D01 T01 §3` (Binary-safe PDF 1.7 and 2.0 lexer (`src/pdf/lexer.cpp`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s3.json` records 3 rounds (fixed, fixed, fixed). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §3`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s3.md`.
2. Audit `D01 T01 §3` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s3.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §3"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s3.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §3`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s3.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s3.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §3`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s3.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §3 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §3"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s3.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 18. Audit D01 T01 §4: Bounded recursive-descent indirect object parser (src/pdf/parser.cpp)

`D01 T01 §4` (Bounded recursive-descent indirect object parser (`src/pdf/parser.cpp`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s4.json` records 3 rounds (fixed, fixed, fixed). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §4`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s4.md`.
2. Audit `D01 T01 §4` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s4.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §4"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s4.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §4`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s4.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s4.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §4`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s4.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §4 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §4"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s4.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 19. Audit D01 T01 §5: Classic xref tables, trailer merging and /Prev incremental update

`D01 T01 §5` (Classic `xref` tables, trailer merging and `/Prev` incremental update chains) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s5.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §5`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s5.md`.
2. Audit `D01 T01 §5` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s5.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §5"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s5.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §5`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s5.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s5.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §5`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s5.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §5 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §5"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s5.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 20. Audit D01 T01 §7: Lossless stream filters (Flate, LZW, predictors, ASCII85, ASCIIHex

`D01 T01 §7` (Lossless stream filters (`Flate`, `LZW`, predictors, `ASCII85`, `ASCIIHex`, `RunLength`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s7.json` records 3 rounds (fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §7`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s7.md`.
2. Audit `D01 T01 §7` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s7.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §7"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s7.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §7`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s7.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s7.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §7`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s7.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §7 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §7"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s7.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 21. Audit D01 T01 §6: PDF 1.5+ /Type /XRef streams, /Type /ObjStm streams and damaged-file

`D01 T01 §6` (PDF 1.5+ `/Type /XRef` streams, `/Type /ObjStm` streams and damaged-file recovery) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s6.json` records 10 rounds (fixed, fixed, fixed, fixed, fixed, fixed, fixed, fixed, fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §6`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s6.md`.
2. Audit `D01 T01 §6` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s6.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §6"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s6.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §6`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s6.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s6.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §6`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s6.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §6 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §6"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s6.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 22. Audit D01 T01 §8: Bi-level and image stream filters (CCITTFaxDecode, JBIG2Decode, WIC

`D01 T01 §8` (Bi-level and image stream filters (`CCITTFaxDecode`, `JBIG2Decode`, WIC `DCT` and `JPX`)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s8.json` records 16 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §8`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s8.md`.
2. Audit `D01 T01 §8` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s8.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §8"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s8.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §8`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s8.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s8.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §8`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s8.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §8 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §8"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s8.md` ends in an approving round or a recorded stop with every finding fixed or filed.

## 23. Audit D01 T01 §9: Standard Security Handler decryption (RC4, AES-128, AES-256 R=2..6

`D01 T01 §9` (Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6` via CNG)) was stamped before 2026-10-21 under the earlier review process: reviews by a different reviewer with a three-round cap. Its receipt `docs/reviews/d01-t01-s9.json` records 5 rounds (fixed, clean). This section re-verifies it under the current process (operator request, 2026-10-21): the section's own Test checkpoint re-run at the current tip, and fresh independent rounds by the pinned reviewer in `review-todo-section`'s audit stance, at most 22 rounds with the severity cutoff. A confirmed stamp stays; a regression flips the row back with a dated blocking note and a filed repair.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Re-run `D01 T01 §9`'s Test checkpoint at the current tip. Done when: its output is quoted in `docs/reviews/06-quality/audit-d01-t01-s9.md`.
2. Audit `D01 T01 §9` with independent rounds through `scripts/review-dispatch.py`. Done when: `docs/reviews/06-quality/audit-d01-t01-s9.md` ends in an approving round or a recorded stop with every finding owned.

- [ ] Re-run the Test checkpoint `python scripts/todo-graph.py resolve "D01 T01 §9"` prints, at the current tip, and quote its result in `docs/reviews/06-quality/audit-d01-t01-s9.md`. Done when: the quoted output shows the checkpoint passing, or the failure is recorded as a regression.
- [ ] Run audit rounds on `D01 T01 §9`'s stamped candidate and every later change to the files it owns, recording each with `--record docs/reviews/06-quality/audit-d01-t01-s9.md`. Done when: a round approves, or the severity cutoff or the 22-round cap stops the loop with its outcome recorded.
- [ ] Fix each finding at its owning layer in a new commit, or file it through `add-todo` with an owner. Done when: no finding in `docs/reviews/06-quality/audit-d01-t01-s9.md` is open without a fix commit or a filed section.
- [ ] On a confirmed regression, add a dated blocking note to `D01 T01 §9`, file the repair, and list the stamped sections that depend on it as suspect in `docs/reviews/06-quality/audit-d01-t01-s9.md`. Done when: the note, the repair section and the suspect list exist, or the audit confirms the stamp.
- [ ] Commit: `review: audit D01 T01 §9 under the current review process`

**Test checkpoint:** `python scripts/todo-graph.py resolve "D01 T01 §9"` exits 3 (still verified) or the blocking note is present, and `docs/reviews/06-quality/audit-d01-t01-s9.md` ends in an approving round or a recorded stop with every finding fixed or filed.

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

## Verification

- Every row above is `[x]`, each with an audit findings file under `docs/reviews/06-quality/`.
- `python scripts/todo-graph.py query rounds` shows no stamped section whose only reviews predate this file without an audit row here.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
