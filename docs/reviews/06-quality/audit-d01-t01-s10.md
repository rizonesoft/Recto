# Audit - D01 T01 §10: pdf::Document page tree, metadata, /Outlines, font list and /Annots links

- **Audit section:** `D06 T05 §24` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §10` (`docs/reviews/d01-t01-s10.json`, earlier process)
- **Date:** 2026-10-23
- **Candidate:** the stamped commit `5e4c50a` and every later change to the files it owns (`src/pdf/document.h`, `src/pdf/document.cpp`, `tests/recto-tests/pdf_document_tests.cpp`): `bd78922`, `9b15890`, `f233e09`, `a5a1430`, `f79830e`, `0ecdc7a`, `b41f474`, `4bdc095`, `3d558a7`, `9aeb2cf`, up to `fa8448a`.

## Checkpoint re-run

`python scripts/todo-graph.py resolve "D01 T01 §10"` -> exit 3 (still verified). The section's own checkpoint at `9117d02`, on the x64 Release AddressSanitizer build `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN` just produced: `recto-tests.exe --gtest_filter=PdfError.*:PdfObject.*:PdfLexer.*:PdfParser.*:PdfXref.*:PdfFilters.*:PdfCrypto.*:PdfDocument.*:Support.AsanActiveWhenEnabled` -> exit 0, `ASan=enabled`, `[  PASSED  ] 116 tests.`, one skipped (`PdfFilters.WicCauseSurvivesAllocationFailure`, whose allocation-failure injection is unavailable under ASan by design). The full gate at the same tree: `.\build.cmd` -> exit 0, 0 Warning(s) in all four configurations, `[  PASSED  ] 191 tests.`

## Completion review: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T17:17:48Z
- Candidate: 9117d02df00ebe9b8a319c92602b7dbe65798baf
- Effort: medium
- Lines: 90
- Digest: d2e314df8e495cc7

The feature is **not complete for a user, and superiority over Acrobat Pro is unproven**. Several document-model gaps would prevent downstream UI from behaving correctly.

This is a static review of `5e4c50a` and the specified later changes through `9117d02`. The three candidate files match that tip. I modified no files and ran no build, tests or competitor benchmarks. “Present” below means source or test coverage exists, not that I verified execution.

**Part 1: completeness**

1. **present: File and memory opening, password detection and authentication.** `src/pdf/document.cpp:1180` implements both input paths; `:1244` supports retaining an unauthenticated document for password retries. Coverage: `PdfDocument.OpenAndAuthenticate`.

2. **present: Basic page order, inherited MediaBox/CropBox/resources, rotation and content access.** Traversal and page construction are at `src/pdf/document.cpp:2298` and `:2441`. Coverage: `PdfDocument.PageTreeAndRotatedPages` and `PdfDocument.PageContents`. Geometry exceptions follow below.

3. **missing-in-scope: Recover readable pages around a damaged branch.** The first failing child propagates out at [document.cpp:2529](R:/GitHub/Recto/src/pdf/document.cpp:2529), and the rollback guard clears previously recovered pages at `:1838`. A document with valid siblings and one cyclic, missing or malformed branch therefore cannot open those readable siblings. Preserve bounded traversal and typed failure when nothing usable remains; add mixed good/bad-branch regressions. The existing wholly cyclic-file refusal test should remain.

4. **missing-in-scope: Report partial results and exactly what was skipped.** `DocumentDescription` has an XRef recovery boolean but no structured page-tree, outline, link or font completeness report (`src/pdf/document.h:167`). Null children are silently skipped at `src/pdf/document.cpp:2524`; other paths silently stop or continue, including outline resolution at `:3160` and font resolution at `:4118`. Consumers need document-associated issue records identifying affected objects/pages and reasons, with tests distinguishing complete, recovered and partial results.

5. **missing-in-scope: Isolate optional-data damage from readable page content.** Oversized Info fields abort opening at `src/pdf/document.cpp:2558`; XMP decompression-limit failures propagate at `:2632`; outline limits also abort at `:3128`. Safely stop the affected extraction and report incompleteness rather than discard otherwise usable pages. Add regressions for valid pages accompanied by malformed or over-budget optional structures.

6. **missing-in-scope: Preserve physical page geometry.** `PageDescription` exposes dimensions as points, but construction copies raw CropBox dimensions without `/UserUnit` (`src/pdf/document.cpp:2452`); the model has no corresponding scale field. `/UserUnit` changes the physical size of default user-space units. Also, BleedBox/TrimBox/ArtBox are incorrectly propagated as inherited attributes at `:2364`, although they are page-local entries. Test scaled pages and page-local production boxes. See [ISO 32000-1, Table 30](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf).

7. **present: Standard Info fields, dates, version and XMP fallback.** Extraction is at `src/pdf/document.cpp:2545`; coverage includes `PdfDocument.Metadata`, namespace handling and bounded entity scanning.

8. **present: Basic hierarchical bookmarks and named destinations.** Extraction starts at `src/pdf/document.cpp:3106`; coverage: `PdfDocument.OutlinesAndNamedDests`. Destination fidelity remains incomplete.

9. **missing-in-scope: Preserve the complete destination view.** `/FitR` retains left/top but discards bottom/right (`src/pdf/document.cpp:2765`). Null XYZ/FitH/FitV operands collapse into concrete defaults, losing “retain the current coordinate” semantics. `PageLink` lacks a fit mode entirely, so links lose even distinctions the outline model retains (`src/pdf/document.h:135`, `src/pdf/document.cpp:3883`). Use a shared destination representation preserving mode, rectangle and optional operands; test every supported mode. These distinctions determine the resulting view in the [Adobe PDF reference](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf).

10. **missing-in-scope: Preserve cross-document destinations.** GoToR extraction retains a filename and, for arrays, only the first page integer (`src/pdf/document.cpp:3604`). Named destinations, destination coordinates/mode and window preference are lost; GoToE target information is also absent. Outline extraction handles only GoTo and URI actions at `:3241`, leaving remote bookmarks without usable target information. Add complete, bounded target extraction and regression fixtures; activation remains D02’s responsibility.

11. **missing-in-scope: Resolve legitimate relative web links before scheme classification.** `is_safe_external_uri` accepts only already-absolute HTTP/HTTPS/mailto strings (`src/pdf/document.cpp:850`), and extraction never applies Catalog `/URI /Base`. A link such as `chapter2.html` with an HTTPS base becomes refused. Resolve against the PDF’s base, then apply the existing safety policy. Test safe and unsafe resulting schemes. Relative resolution is defined in [ISO 32000-1, Table 207](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf).

12. **missing-in-scope: Preserve link interaction restrictions and geometry.** Extraction copies `/Rect` and action information but omits annotation flags and `/QuadPoints` (`src/pdf/document.cpp:3750`, `src/pdf/document.h:135`). D02 consequently cannot implement correct hit regions or distinguish hidden/noninteractive links from ordinary ones using this model. Add flags and quadrilaterals, with regression tests. The standard explicitly prohibits interaction with Hidden and ReadOnly annotations: [ISO 32000-1, Table 165](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf?from=20423&from_column=20423).

13. **present: Inert link actions and bounded hostile-input handling.** Refusal classification and action-chain checks are at `src/pdf/document.cpp:3345` and `:3578`. Coverage: `PdfDocument.LinksAndRefusedActions`, `PdfDocument.BrokenCorpusSweep` and the resource/destination budget cases. This does not establish the missing partial-recovery behavior.

14. **missing-in-scope: Complete and truthful font inventory.** Discovery visits page resources, Form XObjects and AcroForm default resources (`src/pdf/document.cpp:4089`, `:4218`), but misses fonts reachable only through annotation appearances, tiling-pattern resources or Type3 resources. Embedding detection accepts a non-null FontFile reference without establishing that it resolves to a font stream (`:3927`), while Type3 glyph programs have no special classification. Add those resource paths and truthful embedding status, with dedicated fixtures. `PdfDocument.FontInventory` currently covers page/Form/default-resource cases.

15. **missing-in-scope: Cooperative cancellation of document opening and extraction.** The public opening APIs have no cancellation input; file reads and structural scans cannot observe cancellation (`src/pdf/document.cpp:1153`, `:2516`, `src/pdf/document.h:203`). An actor cannot promptly close a tab while this synchronous work continues. Add cancellation through the owning engine operations and verify typed cancellation with no published partial state.

16. **present: Typed basic filesystem and allocation failures.** Missing files, sharing/permission failures, read failures and allocation failures map to errors at `src/pdf/document.cpp:1100` and `:1169`. **missing-in-scope: Regression coverage for lock, access-denied and interrupted/failed reads.** `PdfDocument.OpenAndAuthenticate` checks missing files and authentication but has no assertions for these real OS conditions.

17. **missing-out-of-scope: User entry points and command wiring.** Open menu/toolbar/Home controls, their automation names/tooltips, `Ctrl+O`, Ctrl+K entry, drag-and-drop and Explorer activation belong to **D02 T01 §§5-7, §9; D02 T03 §§2,11-12**. They are outside this `Surface: no` candidate.

18. **missing-out-of-scope: Usable document panels and rendering.** Thumbnails, outline and properties need working populated/empty/loading/partial/error states, navigation and disabled reasons. Owners: **D02 T03 §§4-5,9**. Actual page rendering and font embedding/substitution belong to **D01 T02**.

19. **missing-out-of-scope: Password/error/recovery presentation and link activation.** Retry/cancel password states and actionable open errors belong to **D02 T01 §§10-11**; link confirmation, refusal feedback and navigation belong to **D02 T02 §9**. Partial-result feedback must consume the engine report from item 4.

20. **missing-out-of-scope: Accessible user interaction.** Keyboard navigation, screen-reader names/order, tooltips, contrast, scaling and reduced motion need verification on the consuming surfaces. Owners: **D02**, using **D10** controls. The engine candidate cannot prove these.

21. **missing-out-of-scope: Persistence, settings and reverse paths.** Recent files, reading position and their next-start read-back belong to **D02 T03 §1**; session persistence/recovery to **D02 T01 §§12-14**; consumed settings to **D02 T04**. Metadata editing, undo and atomic save/read-back belong to **D05**, as specified in this review request. Opening this read-only model itself creates no document change requiring undo, output persistence or a full-disk write path.

**Part 2: excellence**

**Comparator: Adobe Acrobat Pro.** It is the strongest documented reference used here, not a benchmark-proven ranking of all four products. Adobe documents bookmarks that restore views or target other documents/web pages, document metadata and font inspection. Properties is reachable with one `Ctrl+D` chord; font inspection then selects the Fonts tab. Sources: [bookmarks](https://helpx.adobe.com/acrobat/using/page-thumbnails-bookmarks-pdfs.html), [properties](https://helpx.adobe.com/acrobat/desktop/edit-documents/edit-pdf-properties/pdf-properties.html), [font inspection](https://www.adobe.com/uk/acrobat/resources/embed-fonts-in-pdf.html?linkId=100000363183091), [properties shortcut](https://experienceleague.adobe.com/en/docs/document-cloud-learn/acrobat-learning/advanced-tasks/forms/workforms). I did not measure its damaged-file recovery or local opening latency.

1. **in-scope: Navigation fidelity falls below the documented view-restoration job.** Items 9-12 lose information before the viewer receives it. **Target:** exact model assertions for all destination modes, null operands, remote named targets, relative HTTPS links, interaction flags and quadrilateral hit regions. Downstream navigation should match reference views without corrective scrolling or zooming.

2. **in-scope: Defaults can produce misleading geometry and font information.** Items 6 and 14 prevent trustworthy dimensions and inventory. **Target:** `/UserUnit 2` doubles physical dimensions; page-local boxes never leak between pages; every font in the resource-path fixtures is reported with correct embedding classification. No settings changes should be necessary.

3. **in-scope: Recovery does not meet Recto’s own stronger ambition.** Items 3-5 turn local damage into total refusal or unexplained omission. This is a confirmed Recto limitation, not a claim that Acrobat recovers every such file. **Target:** retain every safely reachable valid sibling, maintain bounded work, and report each omitted branch or extraction category. Mixed-damage fixtures must verify retained order and issue records.

4. **in-scope: Opening cannot be cancelled promptly.** Item 15 leaves close/cancel waiting for synchronous engine work. **Target:** on controlled CPU-bound large/hostile fixtures, observe cancellation and return `Cancelled` within 100 ms, release resources and publish no successful incomplete model. Test OS I/O cancellation separately.

5. **in-scope: No evidence establishes a speed advantage.** Opening reads the entire file before parsing (`src/pdf/document.cpp:1151`) and completes outline/link/font extraction before marking the structure loaded (`:4236`). That creates first-result latency risk; it does not prove Acrobat is faster. **Target:** same-machine cold/warm comparative measurements, including large and damaged files, with p50/p95 latency and peak memory. Meet the existing **under-300-ms first visible page for a 100-page text PDF** target and demonstrate a reproducible improvement over Acrobat. Keep parsing off the UI thread through **D01 T02 §8**.

6. **out-of-scope: Fewer steps, accessibility and presentation polish remain undelivered.** The candidate has no usable inspection/navigation surface to compare with Acrobat’s properties shortcut and bookmark interaction. **Target:** one chord to properties, at most one additional selection for fonts, one activation per bookmark/link, and no recovery dialog blocking readable pages. Verify keyboard-only and screen-reader flows, partial-result announcements, scaling, high contrast and reduced motion. Owners: **D02/D10**. Any new recovery surface needs its SPEC subsection and board first.

**Consolidated gap list**

1. `missing-in-scope` P1.3: Preserve readable sibling pages around damaged branches, D01 T01 §10.
2. `missing-in-scope` P1.4: Expose structured partial-result and skipped-object diagnostics, D01 T01 §10.
3. `missing-in-scope` P1.5: Isolate optional metadata/outline extraction damage, D01 T01 §10.
4. `missing-in-scope` P1.6: Preserve UserUnit scaling and page-local production-box semantics, D01 T01 §10.
5. `missing-in-scope` P1.9: Preserve destination modes, FitR rectangles and null operands, D01 T01 §10.
6. `missing-in-scope` P1.10: Preserve complete remote and embedded-document targets, D01 T01 §10.
7. `missing-in-scope` P1.11: Resolve relative URI targets before safety classification, D01 T01 §10.
8. `missing-in-scope` P1.12: Preserve link flags and quadrilateral interaction geometry, D01 T01 §10.
9. `missing-in-scope` P1.14: Complete font discovery and embedding classification, D01 T01 §10.
10. `missing-in-scope` P1.15: Add cancellable document opening/extraction, D01 engine.
11. `missing-in-scope` P1.16: Add lock, permission and failed-read regressions, D01 T01 §10.
12. `missing-out-of-scope` P1.17: Deliver Open controls, shortcuts, command entries and activation paths, D02.
13. `missing-out-of-scope` P1.18: Deliver panels and their states, D02; rendering/font substitution, D01 T02.
14. `missing-out-of-scope` P1.19: Deliver password/error/partial-result presentation and confirmed link activation, D02.
15. `missing-out-of-scope` P1.20: Deliver and verify accessible interaction, D02/D10.
16. `missing-out-of-scope` P1.21: Deliver recent/session/settings persistence, D02; metadata undo and atomic save/read-back, D05.
17. `in-scope` P2.1: Prove destination and link fidelity against reference views.
18. `in-scope` P2.2: Make geometry and font defaults accurate without user intervention.
19. `in-scope` P2.3: Retain safely readable content and explain every omission.
20. `in-scope` P2.4: Demonstrate bounded opening-cancellation latency and clean teardown.
21. `in-scope` P2.5: Meet existing performance targets and establish a measured Acrobat advantage.
22. `out-of-scope` P2.6: Beat inspection/navigation step counts and verify accessible, polished feedback, D02/D10.

COMPLETION: gaps

Disposition of the completion review's gaps (each verified against the code before acting):

- Implemented: gap 4 in part, BleedBox, TrimBox and ArtBox are page-local (ISO 32000-1 Table 30) and no longer inherit from `/Pages` nodes; `PdfDocument.ProductionBoxesArePageLocal`. In the commit after `9117d02`.
- Filed: D06 T05 §43 (gap 1, readable siblings kept around a damaged page-tree branch, added to the salvage section)
- Filed: D06 T05 §49 (gaps 2, 3 and 11: partial opening with a bounded issue report, optional-structure damage isolated, lock, access-denied and failed-read regressions)
- Filed: D01 T02 §21 (gap 4 in part, `/UserUnit` in the page model, existing owner)
- Filed: D06 T05 §50 (gaps 5 to 8 and Part 2 item 1: destination modes and null operands, remote and embedded targets, `/URI /Base`, link flags and quadrilaterals)
- Filed: D06 T05 §51 (gap 9 and Part 2 item 2: font inventory over every resource path, checked embedding, Type3)
- Filed: D06 T05 §52 (gap 10 and Part 2 item 4: cancellable opening with a measured 100 ms bound)
- Filed: D06 T01 §7 (Part 2 item 5, first-page latency and comparative measurements, existing owner of the performance targets)
- Filed: D02 T01 §5 (gap 12, open controls, shortcuts and activation paths, existing owner)
- Filed: D02 T03 §4 (gaps 13 and 15, panels with their states and accessible interaction, existing owner)
- Filed: D02 T01 §10 (gap 14, password and open-error presentation, existing owner)
- Filed: D02 T02 §9 (gap 14 and Part 2 item 6, link activation, confirmation and refusal, existing owner)
- Filed: D02 T03 §1 (gap 16, recent files and reading position, existing owner)

## Round 1: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T17:21:16Z
- Effort: medium
- Lines: 37
- Digest: f90793a145fcb55d

Static review of the candidate through `9117d02`. Reproductions below are source-derived, not executed. No files were modified; no build or tests were run.

## adversarial: needs-attention

1. **P2: Hidden links are returned as actionable.** `src/pdf/document.cpp:3760` never checks annotation `/F`, and `PageLink` preserves no flags. Reproduce by opening a one-page PDF containing `/Subtype /Link /Rect [0 0 100 100] /F 2 /A << /S /URI /URI (https://example.com/) >>`, then calling `page_links(0)`. Extraction returns `ExternalUri` with `is_inert == false`, indistinguishable from an interactive link. Hidden annotations must prohibit interaction. [Adobe annotation documentation](https://opensource.adobe.com/dc-acrobat-sdk-docs/library/jsapiref/JS_API_AcroJS.html#hidden)

## consistency: needs-attention

2. **P1: The promised `/Count` validation does not exist.** `src/pdf/document.cpp:2404` creates a traversal frame without retaining the declared count; `:2518` discards completed frames without comparing their descendant totals. Reproduce by opening a PDF whose root is `<< /Type /Pages /Count 2 /Kids [3 0 R] >>` with exactly one valid page. Opening succeeds and reports one page. Missing, negative and incorrect counts on intermediate nodes also pass. This violates the explicit candidate contract to check `/Count` against leaves.

3. **P2: A dangling font-program reference is reported as embedded.** `src/pdf/document.cpp:3928` checks only whether `/FontFile`, `/FontFile2` or `/FontFile3` is syntactically non-null. Reproduce with a page font whose descriptor contains `/FontFile2 99 0 R`, with object 99 absent or free. Open the PDF and inspect `fonts()`: `is_embedded` becomes true despite no embedded font stream existing. Resolve the reference and establish that it identifies a stream before claiming embedding.

## integration: needs-attention

4. **P2: Destination extraction destroys information needed for navigation.** `src/pdf/document.cpp:2765` discards `/FitR` bottom and right; `:2740` replaces null XYZ coordinates with concrete defaults; `:3883` discards the fit mode when producing `PageLink`. Reproduce by opening PDFs with otherwise identical destinations:
   - `[3 0 R /FitR 10 20 100 200]` and `[3 0 R /FitR 10 80 500 200]` produce identical outline destinations despite requiring different fitted views.
   - `[3 0 R /XYZ null null null]` becomes concrete `x = 0`, `y = crop_box.y_max`, losing the instruction to retain the current coordinates.
   - Link destinations `/Fit` and `/FitB` produce identical `PageLink` values.

   Consumers cannot recover these distinctions from the returned model.

## source-defect: needs-attention

5. **P2: `/UserUnit` is ignored when reporting physical dimensions.** `src/pdf/document.cpp:2452` assigns raw box dimensions to fields documented as points, without preserving a unit scale. Reproduce by opening a PDF 1.7 page with `/MediaBox [0 0 100 200] /UserUnit 2`. `page_description(0)` reports `100 × 200` points instead of `200 × 400`. ISO 32000-1 Table 30 defines `/UserUnit` as the scale of default user-space units.

6. **P2: Non-inheritable production boxes leak from `/Pages` into pages.** `src/pdf/document.cpp:2364` propagates `/BleedBox`, `/TrimBox` and `/ArtBox` through `InheritedPageAttrs`. Reproduce with `/TrimBox [10 10 90 90]` on a parent `/Pages`, and a child page with `/CropBox [0 0 100 100]` and no `/TrimBox`. The child receives the parent’s trim box. Under ISO 32000-1 sections 7.7.3.3 to 7.7.3.4, these entries are page-local; an omitted trim box defaults to the crop box.

7. **P2: Valid relative URI links are refused before base resolution.** `src/pdf/document.cpp:3371` checks the raw URI against an absolute-scheme allowlist; extraction never applies Catalog `/URI /Base`. Reproduce by opening a PDF with Catalog `/URI << /Base (https://example.com/manual/) >>` and a link action `/URI (chapter2.html)`. `page_links(0)` returns a refused action instead of `https://example.com/manual/chapter2.html`. ISO 32000-1 section 12.6.4.7 defines this base-resolution behavior.

## design: approve

## record: advisory

The working review record claims 116 ASan tests and 191 full-build tests. Those execution claims were not independently verified in this review. The existing stamp does not establish the missing `/Count` check identified above.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P2, hidden links returned as actionable): correct; it is completion gap 8. Filed: `D06 T05 §50` keeps each link's `/F` flags so a hidden or no-view link is reported as such.
- Finding 2 (P1, `/Count` not checked): the round prompt was wrong, not the stamp. It described `D01 T01 §10` as checking `/Count` against the leaves, but the section's contract (`python scripts/todo-graph.py resolve "D01 T01 §10"`) asks for traversal with inherited attributes and cycle and depth bounds, not a `/Count` check. Refusing a file over a wrong `/Count` would also be the wrong behaviour: the leaves are authoritative, and readers tolerate a mismatched count. So the stamp stands, and the useful part, recording a mismatch as an issue while keeping the leaves, is filed in `D06 T05 §49`.
- Finding 3 (P2, a dangling `/FontFile` reported as embedded): correct; completion gap 9. Filed: `D06 T05 §51`.
- Finding 4 (P2, destination detail lost): correct; completion gap 5. Filed: `D06 T05 §50`.
- Finding 5 (P2, `/UserUnit`): correct; completion gap 4. Filed: `D01 T02 §21`, the existing owner.
- Finding 6 (P2, production boxes inherited): correct, and fixed in this audit: BleedBox, TrimBox and ArtBox are read from each node only and never pass to its pages (ISO 32000-1 Table 30). `PdfDocument.ProductionBoxesArePageLocal`.
- Finding 7 (P2, relative URIs refused): correct; completion gap 7. Filed: `D06 T05 §50`.

None of these is a regression of what `D01 T01 §10` stamped: each is behaviour its contract did not ask for, now owned by a section, apart from finding 6, a defect in what it did build, fixed here. The stamp is confirmed.
