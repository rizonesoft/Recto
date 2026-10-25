## Completion review: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T17:48:30Z
- Candidate: 79c55a2af39e6c369d9b398617af9fe3fcace472
- Effort: medium
- Lines: 53
- Digest: 71d75e3cf8ae5e35

The engine path is implemented, but the feature is not yet complete for a user, and superiority over competitors is unproven. This is a static review of `79c55a2`; tests were inspected, not executed. No files were modified.

**Part 1: completeness**

1. **present: Import compressed-object locations before authentication.** Recovery reads intact xref streams and imports their rows independently of encrypted containers. Evidence: `src/pdf/xref.cpp:1533`, `:1565`, `:1659`; `PdfDocument.RecoveredEncryptedObjectStreamsResolve`.

2. **present: Preserve later definitions and deletions.** Later free rows remove earlier objects, type-1 rows restore scanned uncompressed definitions, and type-2 rows select the named container. Evidence: `src/pdf/xref.cpp:1582`, `:1603`, `:1639`; `PdfXref.RecoveryRoundThreeFixes` and the deletion assertions at `tests/recto-tests/pdf_document_tests.cpp:231`.

3. **present: Defer ciphertext until authentication.** Containers are deferred until parsed trailers establish encryption, then encrypted containers remain undecoded. This also avoids dependence on literal `/Encrypt` spelling. Evidence: `src/pdf/xref.cpp:817`, `:1713`; `PdfXref.RecoveryDeferredContainerEdges`, `PdfXref.RecoveryXrefStreamRowsAndBudgets`.

4. **present: Password refusal, retry, compressed catalog and actual page content.** The regression checks `NeedsPassword`, `WrongPassword`, successful retry on the pending document, page count, decoded content matching the original, and `DamagedFormat` for a non-catalog root. Evidence: `tests/recto-tests/pdf_document_tests.cpp:254`, particularly `:305` through `:328`.

5. **present: Preserve unencrypted recovery.** Dedicated regressions cover compressed catalogs and deferred containers opening with their pages. Evidence: `PdfDocument.RecoveredCompressedCatalogKeepsItsPages` and `PdfDocument.RecoveredDeferredContainerOpensWithItsPages`.

6. **present: Bound hostile input and report unusable xref streams.** Recovery shares a cumulative row budget, limits decoded work, propagates allocation/budget refusals and records discarded streams with their location. Evidence: `src/pdf/xref.cpp:826`, `:1538`, `:1545`, `:1565`; `PdfXref.RecoveryXrefStreamRowsAndBudgets`.

7. **present: Read-only opening and typed filesystem failures.** Missing files, permissions and sharing violations produce typed errors; opening does not write a repaired file. Evidence: `src/pdf/document.cpp:1100`, `:1268`; `PdfDocument.OpenAndAuthenticate`. No recovery-specific setting, undo operation or persistent record is needed for this in-memory operation. Full-disk handling belongs to the repaired-copy workflow below.

8. **missing-out-of-scope: User-facing opening and recovery flow.** The engine still needs Open menu/toolbar integration, `Ctrl+O`, `Ctrl+K`, recent-file and drag-and-drop routes, password retry/cancel, loading and failure states, and honest partial-recovery feedback. Controls need automation names, tooltips and accessible keyboard/focus behavior. Evidence: `src/app/main.cpp:1` explicitly leaves screens and commands to D02; the engine exposes only recovery status at `src/pdf/document.cpp:1303`. Already assigned to **02-viewer**, with **D06 T03 §3** and **D02 T01 §11** for recovery reporting. Require integration tests covering every entry route and dialog state.

9. **missing-out-of-scope: Cancellation and progress.** Recovery has no cancellation/progress interface, so bounded work can still leave a user waiting without a way to stop. Evidence: `src/pdf/xref.h:215`. Already filed as **D06 T05 §42**, owned by engine/core with viewer integration. Its existing target is cancellation within **100 ms**, returning `Cancelled` without publishing a partial table.

10. **missing-out-of-scope: Coverage across supported encryption methods.** The new damaged-file document fixtures use RC4; intact-file AES tests do not establish damaged AES container recovery. Evidence: `tests/recto-tests/pdf_document_tests.cpp:193`, `:256`, versus the intact encryption cases at `:512`. Earlier disposition assigns AES-128/AES-256 recovery fixtures to **D06 T05 §44**, **06-quality**. Require matching pages/content and password/refusal behavior for each supported method.

11. **missing-out-of-scope: Recovery when xref-stream data is also damaged.** Encrypted containers without usable imported rows receive no authenticated reconstruction pass. Evidence: `src/pdf/xref.cpp:1713`. Already filed as **D06 T05 §45**, engine work owned through **06-quality**. This exceeds §30’s intact-xref premise.

12. **missing-out-of-scope: Retain recovery as a verified repaired copy.** There is no completed workflow that saves the recovered result and reopens it without recovery. Already filed as **D06 T05 §43**, with viewer save integration. Evidence: `todo/06-quality/TODO-05-stamp-audits.md:1187`. Require unchanged original bytes, preserved encryption/permissions, temp-file-then-replace, equivalent reopened content, and safe failure on full disk, locked destination, missing permission or interrupted writing.

**Part 2: excellence**

**Comparator: Adobe Acrobat Pro.** Adobe documents damaged-file rebuilding during opening, password authentication with retries, and explicit rebuild failure. That establishes a relevant baseline, not success on Recto’s exact fixtures. No inspected evidence establishes whether Acrobat, Foxit PDF Editor, PDFelement or UPDF is strongest on this corpus, or supplies measured competitor click counts and timings. [Adobe PDDoc documentation](https://opensource.adobe.com/dc-acrobat-sdk-docs/acrobatsdk/apireference/PD_Layer/PDDoc.html#PDDocOpen())

1. **out-of-scope: No measured recovery or speed advantage.** Existing tests establish selected correctness properties, not comparative recovery yield, first-page latency or peak memory. **Target:** complete **D06 T05 §44**, recording exact product builds, recovered pages, median/p95 first-page time and memory on identical damaged encrypted files. Demonstrate a measurable advantage without losing content or weakening refusals. The filed benchmark uses Foxit Reader by owner decision; it cannot independently establish a claim against Foxit PDF Editor.

2. **out-of-scope: No delivered low-effort, accessible user journey.** Engine calls cannot yet compete with Acrobat’s usable password-opening flow. **Target:** after file selection, one password submission and zero separate repair commands for a recoverable file; keyboard-only retry/cancel, named controls, correct screen-reader focus, scaling and contrast checks, and actionable recovery/error wording. Owners: **02-viewer**, **D06 T03 §3**. Measure actual competitor steps before claiming fewer.

3. **out-of-scope: No cancellable recovery feedback.** Allocation bounds do not establish responsive interaction or absence of engine work on the UI thread. **Target:** the existing **100 ms** cancellation gate in **D06 T05 §42**, visible progress during long recovery, and an integration trace showing parsing/decryption off the UI thread.

4. **out-of-scope: Broader salvage and durable recovery remain unfinished.** Files with damaged xref payloads or page trees, and users needing a reusable repaired copy, exceed the delivered path. **Target:** complete **D06 T05 §45/§43**, preserve surviving content with explicit loss reporting, and independently reopen the repaired copy with unchanged original bytes. Competitor superiority remains subject to corpus results.

**Consolidated gaps**

1. **missing-out-of-scope:** Complete opening, password, accessibility and recovery-reporting integration, **02-viewer / D06 T03 §3**.
2. **missing-out-of-scope:** Add cancellable recovery and progress, **D06 T05 §42**.
3. **missing-out-of-scope:** Prove damaged AES-128/AES-256 recovery and refusal behavior, **D06 T05 §44**.
4. **missing-out-of-scope:** Reconstruct encrypted members after authentication when xref payloads are damaged, **D06 T05 §45**.
5. **missing-out-of-scope:** Save and independently reopen a safe repaired copy, **D06 T05 §43**.
6. **out-of-scope:** Establish measured recovery/speed superiority and qualify the Foxit Reader versus Editor comparison, **D06 T05 §44**.
7. **out-of-scope:** Deliver and measure a low-step, accessible password/recovery journey, **02-viewer / D06 T03 §3**.
8. **out-of-scope:** Demonstrate responsive cancellation, progress and off-UI-thread execution, **D06 T05 §42** plus viewer integration.
9. **out-of-scope:** Demonstrate broader salvage and durable recovery against competitor outcomes, **D06 T05 §43/§45**.

COMPLETION: gaps

Completion review dispositions (writer):

- Filed: D02 T01 §10 (gaps 1 and 7: the Password dialog, its keyboard path and screen-reader names; existing owner)
- Filed: D02 T01 §5 (gaps 1 and 7: the shared Open pipeline, one password submission and no separate repair command; existing owner)
- Filed: D06 T03 §3 (gap 1: recovery reporting to the user; existing owner)
- Filed: D06 T05 §42 (gaps 2 and 8: cancellable recovery with progress, off the UI thread; existing owner)
- Filed: D06 T05 §44 (gaps 3 and 6: damaged AES-128 and AES-256 files and the competitor comparison; existing owner)
- Filed: D06 T05 §45 (gaps 4 and 9: encrypted members rebuilt after authentication without an intact xref stream; existing owner)
- Filed: D06 T05 §43 (gaps 5 and 9: a repaired copy that reopens without recovery; existing owner)
