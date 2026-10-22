# Audit - D01 T01 §5: Classic xref tables, trailer merging and /Prev incremental update

- **Audit section:** `D06 T05 §19` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §5` (`docs/reviews/d01-t01-s5.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `b56e4bb`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.Classic*:PdfXref.Incremental*:PdfXref.Cyclic*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 5 tests.` (5 tests from 1 suite). The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T07:25:07Z
- Effort: medium
- Lines: 30
- Digest: 4f348108a322b9a0

### adversarial: needs-attention

1. **P1: Classic xref parsing exposes objects that `/Size` says are missing.** `src/pdf/xref.cpp:1392` inserts every classic entry without applying the trailer’s `/Size`; `src/pdf/xref.cpp:1485` subsequently returns those entries without checking it. `/Size` is copied as metadata but never enforced for classic tables.

   **Reproduce:** In the in-memory fixture from `tests/recto-tests/pdf_xref_tests.cpp:91`, change `/Size 9` to `/Size 4`, retaining object `7 2` and its xref entry. Parse with recovery disabled, then resolve `{7, 2}`. The code returns `"GenerationTwoObject"` instead of treating object 7 as missing. This reproduction is established by source inspection, not an executed modified test.

### consistency: approve

No separate findings.

### integration: needs-attention

Finding **1 (P1)** reaches document consumers: `src/pdf/document.cpp:1489` uses `xref_.find(current.obj_num)` directly, with no `/Size` validation. The same fixture therefore exposes the out-of-range object through document resolution, rather than containing the defect within the table parser.

### source-defect: needs-attention

Finding **1 (P1)** contradicts the trailer semantics: objects numbered above `/Size` must be ignored and treated as missing. The PDF Association’s [ISO 32000-2 Table 15 clarification](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html#75-file-structure) explicitly preserves that requirement. The reproduction uses object 7 with `/Size 4`, avoiding any boundary interpretation.

### design: advisory

Not applicable: this section owns no user surface.

### record: advisory

The existing Release executable reproduced all five checkpoint passes; the additional support test reported `ASan=enabled`. However, these tests do not exercise finding 1. I did not rebuild or independently establish the executable’s source provenance, so this verifies the available binary’s checkpoint result, not a fresh build of `0c5ceef` or the historical compilation claims.

No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P1): correct. ISO 32000-1 Table 15 makes the trailer's `/Size` required and says an object in a cross-reference section numbered greater than it shall be ignored and treated as missing (unchanged in ISO 32000-2; the PDF Association errata keep it). `XrefTable::parse` copied `/Size` but never applied it. The strict path now refuses a missing or invalid `/Size` (non-positive or above 2^32, as the xref stream path already does), handing it to recovery when `allow_recovery` is set, and erases entries numbered above `/Size` from the merged table, recording a `DamagedXRef` diagnostic for each. `document.cpp`'s `xref_.find` therefore sees them as missing. An object numbered exactly `/Size` is kept, since the text says only "greater than" (recorded as a default). The recovery path, which rebuilds the table by scanning objects, is unchanged. `PdfXref.ClassicEntriesAboveSizeAreMissing` drops object 7 under `/Size 4`, keeps it under `/Size 7`, and refuses a trailer without `/Size`; with the erase removed it fails. All 111 tests pass, the corpus included. `D01 T01 §5` carries a dated correction.

Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 111 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 19 tests.`; `python scripts/todo-graph.py validate` -> `valid: 681 sections`.
