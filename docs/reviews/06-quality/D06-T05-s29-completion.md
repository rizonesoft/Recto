## Completion review: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T17:33:50Z
- Candidate: ab341cf88b01ad518ca09d5b9827f554798b0d8f
- Effort: medium
- Lines: 57
- Digest: d71cda07ccbace05

The feature is **not yet complete for a user**, and superiority over competitors is **unproven**. The remaining product gaps mostly belong to already-filed sections. One in-scope verification gap remains: proving recovery preserves actual page content.

This is a source review of `ab341cf`, including the specified diff and prior findings. No files were modified; no build, tests or competitor applications were run. References below identify candidate source or test definitions, not fresh passing results.

**Part 1: completeness**

1. **present: automatic recovery without a separate repair command.** `Document::open_memory` reaches recovery-enabled parsing at `src/pdf/document.cpp:1300`. `PdfXref.DamagedRecovery` covers rebuilding damaged cross-reference data. No additional recovery setting is needed.

2. **present: indirect container parameters and catalog recovery.** `RecoveryResolvesIndirectObjStmParameters`, `RecoveryDeferredContainerEdges`, `RecoveryIndirectObjStmType` and `RecoveryResolverEdges` cover the advertised parameters, forward references, arrays and a meaningful Flate predictor (`tests/recto-tests/pdf_xref_tests.cpp:212`, `279`, `571`, `641`).

3. **present: definition ordering and metadata retention.** Deferred members respect definition positions at `src/pdf/xref.cpp:1050`; metadata selection compares positions at `1135`; catalog fallback selects the latest valid candidate at `2099`. `RecoveryRoundOneFixes` and the parameter-recovery tests cover ordering. The earlier metadata-order finding should not be repeated.

4. **present: bounded hostile-input handling and typed refusal.** The resolver restricts lookup and checks generations at `src/pdf/xref.cpp:1185`; deferred decoding enforces cumulative budgets at `1765`. Ambiguous stream extents return `DamagedFormat` at `2125`. Tests include `RecoveryChargesFailedObjStmDecodes`, `RecoveryResolverEdges`, `RecoveryRefusesWhenReadingsAreCut` and `RecoveryLengthRoundFour`. The refusal must remain intact.

5. **missing-in-scope: regression proof that real page content survives this recovery path.** `PdfDocument.RecoveredDeferredContainerOpensWithItsPages` creates two pages without `/Contents` and asserts only their count (`tests/recto-tests/pdf_document_tests.cpp:367`). The indirect-type and predictor tests inspect catalog markers, not page content. Add a fixture in the allowed `pdf_xref_tests.cpp` with nonempty page content, resolve the recovered catalog through its page tree to `/Contents`, and assert exact decoded content against an undamaged control. Include distinguishable genuine and injected content in the ambiguous-length case. This is a verification gap, not evidence that content currently fails.

6. **present: original preservation and allocation-failure handling.** Recovery consumes immutable bytes (`src/pdf/xref.h:215`); disk opening requests `GENERIC_READ` (`src/pdf/document.cpp:1094`). Allocation failures become `OutOfMemory` (`src/pdf/xref.cpp:2137`). Recovery itself writes nothing, so it needs no undo operation or disk-space allocation for a repaired file.

7. **missing-out-of-scope: ordinary user entry points and responsive opening.** The app remains a host whose commands arrive in domain 02 (`src/app/main.cpp:1`). Open menu/toolbar actions, their automation names and tooltips, `Ctrl+O`, the `Ctrl+K` entry, drag-and-drop, recent-file activation and loading-state integration are not delivered by this section. Existing owners: **02-viewer**, particularly `D02 T01 §5`, `§6`, `§9` and `D02 T03`; the shared pipeline must dispatch parsing off the UI thread. Verify every entry point reaches the same recovery outcome.

8. **missing-out-of-scope: document-specific partial-recovery information.** A recovered boolean (`src/pdf/xref.h:295`) does not explain discarded containers, known losses or unknown completeness. Existing filing: `D06 T03 §2`, owned by **01-engine**, with quality gates in **06-quality**. Require bounded per-document reasons and tests that distinguish complete recovery, partial recovery and refusal.

9. **missing-out-of-scope: accessible recovery and failure presentation.** Users still need recovery/partial-loss notices and actionable refusal dialogs, including missing permission and locked-file failures, without confusing them with damaged content. Existing owners: **02-viewer**, `D02 T01 §11`, and `D06 T03 §3`. Verify loading, success, partial and error states; keyboard focus; screen-reader announcements; names/tooltips for actions; scaling, contrast and reduced motion. A new recovery-success surface needs its SPEC treatment and board.

10. **missing-out-of-scope: cancellation and progress.** `recover_damaged_xref` has neither interface (`src/pdf/xref.h:215`). Work limits do not let a user stop an expensive open. Already filed as `D06 T05 §42`, owned by **01-engine**, with **02-viewer** integration. Require `Cancelled` within 100 ms during scanning, deferred resolution and decoding, with no partial document published.

11. **missing-out-of-scope: compressed parameter targets.** Recovery explicitly accepts only uncompressed parameter targets (`src/pdf/xref.cpp:1185`). An independently recoverable container holding another container’s parameters remains excluded. Already filed as `D06 T05 §41`, owned by **01-engine**. Keep its bounded dependency and cycle tests; its indirect `/Type` portion is already implemented and is not a remaining gap.

12. **missing-out-of-scope: salvage and durable repaired-copy output.** Rebuilding the table does not salvage a broken page tree or save a copy that opens normally next time. Already filed as `D06 T05 §43`, owned by **01-engine**, with **02-viewer** actions. Verify retained page order/content, temp-file-then-replace, reopening without recovery, and unchanged original bytes after full-disk, permission, locked-destination or interrupted-write failures. Until a copy exists, reopening after a crash repeats recovery from the untouched source.

**Part 2: excellence**

**Comparator: Adobe Acrobat Pro first.** Adobe documents repair-enabled opening through `PDDocOpen(..., doRepair=true)`, rebuild-failure outcomes, a repaired-document flag and saving APIs. That establishes a comparable repair workflow, but does not prove Acrobat’s results on these fixtures. No matched execution here establishes whether Acrobat Pro, Foxit PDF Editor, PDFelement or UPDF is strongest, or their exact recovery click counts. [Adobe Acrobat SDK](https://opensource.adobe.com/dc-acrobat-sdk-docs/acrobatsdk/apireference/PD_Layer/PDDoc.html).

1. **out-of-scope: no measured recovery or speed advantage.** Budget tests and catalog assertions do not establish time to usable content, retained content or peak memory against competitors. **Target:** identical damaged files and undamaged controls; exact product/build and host records; content-retention results, median/p95 first-page time and peak memory; demonstrate at least one safe recovery advantage or materially faster performance without losing content. Existing filing: `D06 T05 §44`, **06-quality**. Its chosen Foxit baseline is Reader, so an Editor-specific superiority claim would still need Editor evidence.

2. **out-of-scope: no delivered effortless opening and cancellation experience.** The engine can recover automatically, but the user-facing pipeline and cancellable progress remain absent. **Target:** zero additional repair actions after ordinary Open for recoverable files; no parsing on the UI thread; cancellation within the already-filed 100 ms target. Measure actual steps against Acrobat before claiming fewer. Owners: **01-engine/02-viewer**, `D06 T05 §42` and `D02 T01 §5`.

3. **out-of-scope: recovery feedback cannot yet establish user trust.** There is no delivered explanation of what survived, what may be missing, or why safe recovery was refused. **Target:** one nonblocking recovery notice, explicit known losses and unknowns, actionable refusal text, document-specific support details, complete keyboard/screen-reader coverage and approved visual states across themes/scales. Owners: **01-engine/02-viewer**, `D06 T03 §2`, `§3` and `D02 T01 §11`.

4. **out-of-scope: incomplete recovery envelope and durable result.** Compressed parameter dependencies, broken page-tree salvage and a verified repaired copy remain future work. **Target:** recover supported acyclic dependencies within unchanged budgets; retain every verifiable surviving page; save a copy that reopens without recovery and matches retained content, while preserving the original through write failures. Owners: **01-engine/02-viewer**, `D06 T05 §41` and `§43`. These are product shortfalls, not a claim that Acrobat recovers every such file.

**Consolidated gap list**

1. **missing-in-scope:** Prove nonempty page-content preservation and genuine-content selection through the indirect-parameter recovery path.
2. **missing-out-of-scope:** Deliver shared Open entry points, accessible controls and off-UI-thread opening, **02-viewer**.
3. **missing-out-of-scope:** Provide bounded document-specific recovery/loss reports, `D06 T03 §2`, **01-engine/06-quality**.
4. **missing-out-of-scope:** Present accessible recovery, partial-success and actionable failure states, `D06 T03 §3` and `D02 T01 §11`, **02-viewer**.
5. **missing-out-of-scope:** Add cancellable recovery and progress, `D06 T05 §42`, **01-engine/02-viewer**.
6. **missing-out-of-scope:** Resolve compressed parameter dependencies safely, `D06 T05 §41`, **01-engine**.
7. **missing-out-of-scope:** Salvage pages and save/reopen an atomic repaired copy, `D06 T05 §43`, **01-engine/02-viewer**.
8. **out-of-scope:** Demonstrate recovery and performance superiority on matched files, `D06 T05 §44`, **06-quality**.
9. **out-of-scope:** Deliver and measure zero-extra-action recovery with responsive cancellation, **01-engine/02-viewer**.
10. **out-of-scope:** Deliver trustworthy, accessible recovery feedback and verified visual states, **01-engine/02-viewer**.
11. **out-of-scope:** Broaden safe recovery and provide a durable repaired result, `D06 T05 §41` and `§43`, **01-engine/02-viewer**.

COMPLETION: gaps

Completion review dispositions (writer):

- Implemented: gap 1 (page content through this recovery path): `PdfXref.RecoveryKeepsPageContent` walks the recovered root through its page tree to `/Contents` and reads the same bytes as an undamaged control, for a catalog held only in a container with an indirect `/Type` and for the ambiguous-length case, where the genuine page's content is read and the injected one's is not.
- Filed: D06 T03 §2 (gap 3: a document-specific recovery and loss report; existing owner)
- Filed: D06 T03 §3 (gaps 4 and 10: accessible recovery, partial-success and refusal states, with D02 T01 §11 for the dialog; existing owner)
- Filed: D02 T01 §11 (gaps 2 and 4: the Open entry points, off-UI-thread opening and the failure dialog; existing owner)
- Filed: D06 T05 §42 (gaps 5 and 9: cancellable recovery with progress; existing owner)
- Filed: D06 T05 §41 (gaps 6 and 11: compressed parameter dependencies; existing owner)
- Filed: D06 T05 §43 (gaps 7 and 11: page salvage and a verified repaired copy; existing owner)
- Filed: D06 T05 §44 (gap 8: recovery measured against the competitors; existing owner)
