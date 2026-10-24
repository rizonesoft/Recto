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

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T09:23:48Z
- Effort: medium
- Lenses: adversarial
- Lines: 10
- Digest: d55abeab48eea9e8

### adversarial: needs-attention

1. **P1: Recursive outline traversal retains gigabytes outside its memory budget.** `src/pdf/document.cpp:3335` recurses while each ancestor’s complete `item_val` remains alive. The budget at `:3324` counts only the extracted `OutlineNode`, excluding those dictionaries. Reproduce by opening a one-page PDF with 64 nested outline items linked through `/First`, each containing a direct `/Pad` string of 128 MiB, stored in separate Flate-compressed object streams. Each object stays below the parser and decoding limits, and the extracted titles stay below the outline budget, but traversal retains **8 GiB of padding alone**. Object-stream cache eviction cannot release these parsed dictionaries. Bound aggregate live traversal memory or discard unused dictionary payload before descending.

2. **P1: Shared non-link annotations bypass the document-wide work limit.** `src/pdf/document.cpp:3732` traverses annotations separately for every page, resets deduplication at `:3752`, and skips non-links at `:3791` without charging the document-wide counter. Reproduce by opening a PDF with 100,000 small page dictionaries, all referencing one indirect `/Annots` array containing 65,536 distinct references to small `/Subtype /Text` annotation dictionaries. These counts fit the existing page, array, xref and retained-page limits. Opening performs **6,553,600,000 annotation resolutions**, repeatedly parsing the same objects, without reaching the link budget. This creates a CPU denial of service from a comparatively small file. Apply a document-wide annotation work budget or reuse classification results across pages; this is separate from cancellable opening.

Both findings are based on static tracing at `dc5cc29`. Reproductions and ASan tests were not executed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P1, outline ancestors kept alive): correct. The outline is now walked with an explicit stack (one frame per open level, holding only the `IndirectRef` it reads next, ISO 32000-1 Table 153), and an item's dictionary is released before its children are read. Writing the test also found the recursive walk overflowing the 1 MB stack of a Debug build at 64 levels (`0xC00000FD`), which the explicit stack removes. `PdfDocument.OutlineTraversalHoldsOneItemAtATime`: 64 nested items with 10,000-element `/Pad` arrays; peak 451,602,325 bytes on the code before the fix against a bound of 51,209,335.
- Finding 2 (P1, shared non-link annotations read once per page): correct. A page whose `/Annots` names an array already read (by the reference it names or the object that resolves to) copies that page's links through the link budget, and an annotation object found not to be a link is not read again on any page. The same class in page-tree validation is fixed too: another indirect page entry is validated once per object, not once per page. `PdfDocument.SharedAnnotationsAreReadOnce`: 1,000 pages sharing one array of 1,001 annotations and 300 pages listing 300 of them directly; 13,192,954,915 bytes allocated on the code before the fix, and with only the validation hunk removed the baseline allocates 2,258,237,779 bytes against 243,390,211.

Gates after the round 2 fixes: ASan engine checkpoint -> exit 0, `0 Warning(s)`, `ASan=enabled`, `[  PASSED  ] 129 tests.` (one skipped by design); `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 210 tests.` in Debug and Release.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T09:57:14Z
- Effort: low
- Lenses: adversarial
- Lines: 8
- Digest: afe5494001c01ae6

### adversarial: needs-attention

1. **P1: Name-tree traversal retains gigabytes outside its memory budget.** `src/pdf/document.cpp:3074` recurses while each ancestor’s complete `resolved_node_val` remains alive. Reproduce by opening a one-page PDF whose `/Names /Dests` tree has 64 nodes linked through `/Kids`, each containing a direct 128 MiB `/Pad` string and stored in a separate Flate-compressed object stream. Each object fits the 256 MiB parser and decode limits, and the tree fits the depth and node limits, but traversal retains **8 GiB of padding alone**, potentially exhausting memory. The named-destination budget counts extracted destinations, not these live dictionaries. Release unused node payload before descending or enforce an aggregate traversal-memory budget. The outline repair does not cover this separate traversal.

Static finding against `29df9ea`; reproduction and ASan tests were not executed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

- Finding 1 (P1, name-tree ancestors kept alive): correct. Three rounds running found one more traversal holding resolved values across its descent (page dictionaries, outlines, the name tree), so every traversal in `src/pdf/document.cpp` was checked and fixed as a whole rather than this one alone: the `/Names /Dests` tree is walked with an explicit stack whose frames hold only their node's `/Kids` array (charged to a live traversal budget of `kMaxDecodedStreamBytes`) and release each node's dictionary once its `/Names` are read; the Form XObject font scan is walked with an explicit stack whose frames hold only the XObjects' references, releasing each stream before its `/Resources` are scanned; and the page-tree stack charges every open level's `/Kids` array to the same kind of budget, refusing with `ResourceLimit`. The action-chain walker was already iterative and holds only parts of the current object, so it is unchanged. No recursive traversal of document objects is left (only the in-memory size estimator recurses, bounded by the parser's depth). Tests, each failing on `29df9ea`: `PdfDocument.NameTreeTraversalHoldsOneNodeAtATime` (peak 89,278,469 bytes before against a bound of 10,244,868), `PdfDocument.FormXObjectFontScanHoldsOneStreamAtATime` (134,327,716 against 83,891,961), `PdfDocument.OpenPageTreeLevelsShareOneBudget` (`DamagedFormat` before, `ResourceLimit` after).

Gates after the round 3 fixes: ASan engine checkpoint -> exit 0, `0 Warning(s)`, `ASan=enabled`, `[  PASSED  ] 132 tests.` (one skipped by design); `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 213 tests.` in Debug and Release.

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T10:13:12Z
- Effort: medium
- Lenses: adversarial
- Lines: 8
- Digest: 98dcc5f55578f95c

### adversarial: needs-attention

1. **P1: Pending action siblings bypass aggregate memory limits.** `src/pdf/document.cpp:3508` deep-clones each `/Next` array element into the traversal stack at `:3513`. Only node count and depth are bounded, not retained bytes. Reproduce by opening a one-page PDF with a link whose action starts 32 chained `/GoTo` dictionaries, each containing `/Next [next_ref << /S /GoTo /D [page_ref /Fit] /Pad (128 MiB of repeated text) >>]`, stored in separate Flate-compressed object streams. Reverse enqueue order visits `next_ref` first, leaving each padded sibling pending. This retains **4 GiB of padding alone**, within the 256-node, 64-level and individual parser/decode limits, potentially exhausting memory. Object-stream cache eviction cannot release these clones. Bound aggregate pending-action bytes before cloning.

Static finding against `874ef18`; reproduction and ASan tests were not executed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 4

- Finding 1 (P1, pending action siblings unbudgeted): correct, and a miss in the round 3 sweep, which judged the action-chain walker safe because it is iterative: its pending `/Next` values are copies that outlive the object they came from while the walk follows a reference sibling. Pending copies now share a 1 MiB budget, charged before each copy and released as each is inspected; a chain past it fails closed (the link is inert), like one past the walker's node and depth limits. `PdfDocument.PendingActionSiblingsShareOneBudget`: 32 chained GoTo actions each leaving a direct sibling with a 2,000-element `/Pad` array pending give an inert link (an `Internal` link on `874ef18`), and the same chain with one-element pads stays `Internal`.

Gates after the round 4 fix: ASan engine checkpoint -> exit 0, `0 Warning(s)`, `ASan=enabled`, `[  PASSED  ] 133 tests.` (one skipped by design); `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 214 tests.` in Debug and Release.
