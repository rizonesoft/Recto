# Audit - D01 T01 §10: rounds under the Astra pin and review schedule

- **Audit section:** `D06 T05 §24` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §10` (`docs/reviews/d01-t01-s10.json`, earlier process)
- **Earlier record:** `docs/reviews/06-quality/audit-d01-t01-s10.md` holds the checkpoint re-run, the completion review and GPT-6.1 Sol rounds 1 and 2, recorded before the pin's `[schedule]` existed; their fixes (`77a2506`, `aed408b`) stand. The regular rounds restart here so the schedule replays from round 1 (`docs/phase-runs/2026-10-24-phase-1.md`, Critical events).
- **Candidate:** the stamped commit `5e4c50a` and every later change to `src/pdf/document.h`, `src/pdf/document.cpp` and `tests/recto-tests/pdf_document_tests.cpp`, up to `aed408b`.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T08:55:32Z
- Effort: high
- Lenses: adversarial, integration, source-defect, plan-delivery
- Lines: 20
- Digest: e830008bb5ef1e4d

### adversarial: needs-attention

1. **P1: Retained page dictionaries bypass aggregate memory limits.** [src/pdf/document.cpp:2462](/R:/GitHub/Recto/src/pdf/document.cpp:2462) copies each complete page dictionary into `page_records_`. The page-count limit does not bound their combined payload, and the resource-dictionary budget does not cover these copies. Reproduce with a valid xref-stream PDF containing 5,120 distinct pages, each carrying a direct `/Pad` string of 1 MiB, packed 32 pages per Flate-compressed `/ObjStm`. Each container stays below the 256 MiB decode limit, but opening retains **5 GiB of padding alone**, potentially exhausting memory. Object-stream cache eviction cannot release the retained page copies. Enforce an aggregate retained-page byte budget before copying.

### integration: needs-attention

2. **P2: Legal indirect annotation and action names lose links.** [src/pdf/document.cpp:3752](/R:/GitHub/Recto/src/pdf/document.cpp:3752) reads `/Subtype` with `get_name()`, which does not resolve references. Reproduce by opening a one-page PDF with `/Annots [4 0 R]`, where object 4 contains `/Subtype 5 0 R /Rect [0 0 10 10] /Dest [3 0 R /Fit]`, and object 5 is `/Link`. `page_links(0)` omits the annotation. Similarly, direct `/Subtype /Link` with `/A << /S 6 0 R /D [3 0 R /Fit] >>` and object 6 equal to `/GoTo` becomes `RefusedAction` at [src/pdf/document.cpp:3356](/R:/GitHub/Recto/src/pdf/document.cpp:3356). Resolve these values through the owning document before classification. ISO permits indirect values unless specifically prohibited. [ISO 32000-1 §7.3.10](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf#page=30)

### source-defect: needs-attention

3. **P2: Distinct byte-string destinations still overwrite each other within the string namespace.** [src/pdf/document.cpp:3011](/R:/GitHub/Recto/src/pdf/document.cpp:3011) converts name-tree keys to Unicode; lookup repeats that conversion at line 3056. Reproduce with `/Names /Dests /Names [<41> [3 0 R /Fit] <FEFF0041> [4 0 R /Fit]]` and bookmarks targeting those two strings. Both keys decode to `A`, so both bookmarks resolve to page object 4. This is separate from the repaired name-versus-string namespace collision. Preserve raw bytes: name-tree equality is byte-for-byte, not decoded-text equality. [ISO 32000-1 §§7.9.6, 12.3.2.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf#page=98)

### plan-delivery: needs-attention

Findings 1–3 affect the audited contract’s bounded opening, link extraction and named-destination resolution. Existing tests do not cover these reproductions.

These are static findings against `aed408b`; the C++ reproductions and ASan/build gates were not executed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P1, retained page dictionaries unbounded): correct, a defect in what `D01 T01 §10` built (bounded opening). Every page dictionary is charged to one document budget before it is copied (`kMaxTotalPageDictBytes`, equal to `kMaxDecodedStreamBytes`, 256 MiB, about 2.6 KiB per page at the 100,000-page cap), and a file past it is refused with `ResourceLimit` naming the page object. `PdfDocument.RetainedPageDictionariesShareOneBudget`: 400 pages with a 20,000-element direct `/Pad` array each (a 16 MiB file) are refused, and the same file with 100-element arrays opens; on the code before the fix the three new tests fail (`NameTreeKeysCompareAsBytes`, `IndirectNamesInAnnotationsActionsAndFonts`, `RetainedPageDictionariesShareOneBudget`, the last after retaining every page for 99.8 s under ASan).
- Finding 2 (P2, indirect `/Subtype` and `/S` lose links): correct. `Document::entry_name` reads a name through indirect references (ISO 32000-1 section 7.3.10), and every name read in `src/pdf/document.cpp` uses it: the catalog and page-tree `/Type`, annotation `/Subtype`, action `/S` and `/N`, additional-action `/S`, font `/Subtype`, `/BaseFont`, `/Name`, `/BaseEncoding`, descendant `/BaseFont`, and Form XObject `/Subtype`. A `/Launch` reached by reference is still refused. `PdfDocument.IndirectNamesInAnnotationsActionsAndFonts`.
- Finding 3 (P2, byte-distinct name-tree keys collide): correct. Name-tree keys and string destinations are kept as their bytes (ISO 32000-1 sections 7.9.6 and 12.3.2.3), so `<41>` and `<FEFF0041>` are two destinations. `PdfDocument.NameTreeKeysCompareAsBytes`.

Gates after the round 1 fixes: `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter 'PdfError.*:PdfObject.*:PdfLexer.*:PdfParser.*:PdfXref.*:PdfFilters.*:PdfCrypto.*:PdfDocument.*:Support.AsanActiveWhenEnabled'` -> exit 0, `0 Warning(s)`, `ASan=enabled`, `[  PASSED  ] 127 tests.` (one skipped, `PdfFilters.WicCauseSurvivesAllocationFailure`, by design under ASan); `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 208 tests.`
