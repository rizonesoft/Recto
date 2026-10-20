---
schema_version: 1
id: user-failure-gates
domain: 06-quality
status: active
title: "TODO-03 -- User-Reported Failure Class Gates (Lessons from pdfcraft)"
---

# TODO-03 -- User-Reported Failure Class Gates (Lessons from pdfcraft)

> **Goal:** Every failure class that pdfcraft's users hit hardest (1,710 issues reviewed in `docs/research/2026-10-20-pdfcraft-issue-lessons.md`) has a Recto gate that fails before the bug can ship: hostile-input and recovered-error fixtures in the engine, a differential proof that every consumer reads content the same way, fault-injected saves, provably complete redaction, Unicode authoring round trips, UI glyph coverage, a printer driver matrix, render and text fidelity fixtures, and a clean-machine smoke of the installed artifacts. The gates test behaviour owned by other sections; a failing gate is fixed in its owner, never by loosening the gate.

> **Current state:** Measured 2026-10-20. No Recto section yet names these failure classes as test fixtures. `tests/corpus/broken/` (`D00 T03 §4`) holds generic malformed files, `D06 T01` holds goldens, text tables, round trips and performance, and `D06 T02` holds the hostile-input audit and fuzzing; none of them carries the pdfcraft classes listed in the research record (split `/Contents`, inline `EI`, silent truncation, save-before-verify, redaction side channels, `?` substitution, single-probe font fallback, printer enumeration hangs, nearest-neighbour shrink, packaged-model gaps).

## Inputs

- `docs/research/2026-10-20-pdfcraft-issue-lessons.md`: the failure classes, pdfcraft issue numbers, and the requirement each sets. Every fixture below cites the pdfcraft issues it reproduces in `tests/corpus/PROVENANCE.md`.
- Foundation plan sections 4 (actor and `DisplayList` contract), 8 (performance) and 9 (hostile input); AGENTS.md architecture rules (typed `PdfResult<T>`, temp-file-then-replace, true redaction); ISO 32000-2 sections 7.8.2 (content streams), 8.9.7 (inline images), 9.10 (text extraction), 11 (transparency), 12.5 (annotations) and 14.8 (tagged PDF).
- -> XREF: D01 T02 §4 -- the content-stream interpreter and `DisplayList` builder whose inline-image, operand and recovery behaviour §1, §2 and §4 here gate.
- -> XREF: D06 T02 §3 -- the scheduled fuzz workflow that takes §1's fixtures as seeds.
- -> XREF: D06 T01 §1 -- the golden harness §10 extends with user-reported render classes and §12 runs release over release.
- -> XREF: D03 T01 §1 -- `PageText` extraction and reading order gated by §4 and §11.
- -> XREF: D02 T01 §8 -- Save, Save As and Replace-Original paths gated by §5.
- -> XREF: D04 T02 §1 -- `AtomicFileWriter`, the write primitive §5 fault-injects.
- -> XREF: D05 T01 §10 -- the physical redactor and sanitizer gated by §6.
- -> XREF: D07 T01 §2 -- the print pipeline gated by §9.
- -> XREF: D07 T03 §5 -- the release gate that refuses a tag without §12's green smoke report.

## Outcome

- `recto-tests.exe --gtest_filter=UserClasses.*` runs every gate in this file; each gate names the pdfcraft issues it reproduces, and each fails on a planted regression of its class (proven once per gate with the `mutation-tester` agent).
- A malformed or capped page never renders blank without a diagnostic, never reports `complete` after a truncation, and never lets a redaction apply past content it could not read.
- Save, redaction and text authoring are proven on raw bytes, not on what Recto's own reader shows back.
- The release candidate is smoke-tested as installed on a clean machine: launch, OCR from the packaged models, CJK UI, print, close and upgrade.

**Adjacency:** all=not-applicable (test gates over behaviour owned by other sections; the one surface, §3, is a read-only page notice with no records, settings or exchange of its own)

**Adjacency rationale:** These sections add fixtures and assertions, not features. Where a gate exposes a missing behaviour, the fix lands in the owning section named in the gate's context paragraph, and that owner's own adjacency covers it.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Hostile-input fixture pack from user-reported failure classes | D01 T02 §4, D01 T03 §4 | [ ] |
| 2 | §2 | Recovered-error and truncation contract in `recto-pdf` results | §1 | [ ] |
| 3 | §10 | Render fidelity fixtures from user-reported failure classes | D06 T01 §1, §1 | [ ] |
| 4 | §3 | Page notice for recovered or truncated content | §2, D02 T02 §4 | [ ] |
| 5 | §8 | UI glyph coverage, non-Latin file names and input conventions suite | D02 T01 §3, D02 T03 §1, D02 T03 §5, D02 T02 §6 | [ ] |
| 6 | §11 | Text extraction, reading order and selection fidelity fixtures | D06 T01 §3, D03 T01 §3, D03 T01 §5, §2 | [ ] |
| 7 | §4 | Single-interpreter differential suite across every content consumer | §1, D03 T01 §9, D05 T01 §10 | [ ] |
| 8 | §5 | Save integrity fault-injection suite | D04 T02 §1, D02 T01 §8 | [ ] |
| 9 | §6 | Redaction completeness and audit suite on raw bytes | §4, D05 T01 §10 | [ ] |
| 10 | §7 | Unicode authoring round-trip suite for every text-writing path | D03 T01 §9, D03 T03 §2, D03 T03 §6, D05 T01 §6, D05 T01 §8 | [ ] |
| 11 | §9 | Printer driver compatibility and print failure isolation suite | D07 T01 §2, D07 T01 §3 | [ ] |
| 12 | §12 | Clean-machine release smoke and upgrade persistence proof | D07 T01 §6, D07 T01 §7, D07 T01 §8, D07 T01 §9, D05 T02 §10, D07 T02 §7, §8, §10 | [ ] |

## 1. Hostile-input fixture pack from user-reported failure classes

pdfcraft crashed, hung or silently dropped content on inputs its users met in the wild (research record, "Hostile numbers and graphs", "Inline image `EI`" and "One bad page-tree entry"). This section turns each reported shape into a deterministic fixture generated by `scripts/build-corpus.py`, so Recto's engine is proven against them before the viewer exists. Each fixture must open or fail with a typed `PdfError`, under AddressSanitizer and a hard memory cap; none may abort, recurse on document-controlled depth, or allocate past the plan section 9 bounds. Fixes land in the engine section that owns the code path (`D01 T01`, `D01 T02`), never by deleting a fixture.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Extend `scripts/build-corpus.py` with a `user_classes` generator writing `tests/corpus/broken/user-classes/*.pdf` and one `PROVENANCE.md` row per file citing the pdfcraft issues it reproduces. Done when: `python scripts/build-corpus.py --check` regenerates byte-identical files and `Corpus.Provenance` passes.
2. Add `UserClasses.HostileInput` in `tests/recto-tests/user_classes_hostile.cpp` opening, rendering every page and extracting text from each fixture. Done when: every fixture returns `PdfResult` success or a named `ErrorCode`, ASan reports nothing, and peak working set stays under the plan section 9 cap.

- [ ] Generate overflow fixtures in `scripts/build-corpus.py`: `cm` with `9223372036854775807`, `/W [4294967295 [...]]`, xref object number `4294967295`, `/Differences` code `300`, NaN and `1e39` coordinates, `/MediaBox [0 0 1e30 1e30]`, `/UserUnit 1e20` (pdfcraft #1313, #1337, #1314, #1306, #1761, #2670). Done when: each file exists under `tests/corpus/broken/user-classes/` with a `PROVENANCE.md` row.
- [ ] Generate decoder fixtures: RC4 `/Length 256`, negative crypt-filter `/Length`, ASCII85 `z` expansion past the output budget, CCITT `/Rows 0` and `/Rows` smaller than `/Height`, JPX with a palette and with `CIEJab`, JPEG2000 corner on the origin (pdfcraft #1769, #1816, #1796, #2352, #3560, #3179, #3306, #2423). Done when: each file exists with a provenance row.
- [ ] Generate graph fixtures: 100,000 duplicate `/Kids` with large direct `/Resources`, a cyclic `/Indexed` base, a 1,000,000-deep acyclic indirect chain, a 16,384-node named-destination chain, a trailing singleton in `/ParentTree /Nums`, `/Prev` self-reference (pdfcraft #1798, #1799, #1800, #3309, #1809, #2919). Done when: each file exists with a provenance row.
- [ ] Generate content-stream fixtures: inline image whose samples contain `EI `, `>EI` with no whitespace, a byte above `0x7F` after `EI`, `l` and `c` before `m`, `re` with three operands, `--280` kerning, tiling `/BBox` 100000 by 10 with step 10, pattern step `2147483647` (pdfcraft #3107, #3108, #2456, #3581, #3568, #3565, #1770, #3349). Done when: each file exists with a provenance row.
- [ ] Generate page-tree fixtures: one unreadable `/Kids` entry among valid pages, `/Contents [ok missing ok]`, `/Annots [a null b]`, an xref table numbered from 1 that starts with object 0's entry (pdfcraft #2660, #2722, #2676). Done when: each file exists with a provenance row and the expected page count is recorded beside it.
- [ ] Add `UserClasses.HostileInput` in `tests/recto-tests/user_classes_hostile.cpp` iterating the directory, opening, rendering each page through `recto-cli render` internals and extracting text with a 5 s per-file deadline. Done when: no fixture aborts, hangs, or exceeds the memory cap under the ASan build.
- [ ] Assert that inline-image length is computed from `/W`, `/H`, `/BPC` and `/CS` (and `/L` when present) for unfiltered data, so the `EI`-in-samples fixture draws no injected text. Done when: `UserClasses.InlineImageLength` finds zero text runs on that page.
- [ ] Assert that the bad-`/Kids` fixture keeps every readable page and reports the skipped entry count. Done when: `UserClasses.PageTreeSkip` sees the recorded page count and one `RecoveredEntry` diagnostic.
- [ ] Add every fixture to the `fuzz_open` and `fuzz_render` seed corpora (`fuzz/corpus/`). Done when: the CI fuzz smoke loads them as seeds without a finding.
- [ ] Commit: `test(quality): add user-reported hostile-input fixture pack and UserClasses.HostileInput`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.HostileInput:UserClasses.InlineImageLength:UserClasses.PageTreeSkip` passes under the ASan build, and reverting the inline-image length computation makes `UserClasses.InlineImageLength` fail.

## 2. Recovered-error and truncation contract in `recto-pdf` results

pdfcraft's users saw blank pages with no message, and its search said `complete: true` after a cap cut it short (research record, "Silent truncation and success on failure"; pdfcraft #623, #971, #3052, #3119, #3751). Recto's engine results carry the evidence instead: a page's `DisplayList` holds a bounded list of `RecoveryNote` records (skipped operator, capped stream, skipped page-tree entry, unmapped glyph), and any capped walk returns a typed `Truncated` outcome that every consumer must propagate. The owning code lives in `D01 T02`; this section adds the contract tests and the shared type at the engine level. Propagation into `PageText` and search results is proven in §11, once those exist (`D03 T01`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `recto::pdf::RecoveryNote` and `RecoveryNotes` (bounded at 256 entries with an overflow count) in `src/pdf/recovery.h`, attach them to `DisplayList` and to the page result of `Document::open`. Done when: the header compiles in `recto-pdf` and `DisplayList::recovery_notes()` is reachable from tests.
2. Add `UserClasses.RecoveryContract` asserting that every §1 fixture that renders partially carries at least one note, and that a fully valid corpus page carries none. Done when: the test passes and a planted silent `return` in the interpreter's operand error path makes it fail.

- [ ] Author `src/pdf/recovery.h` with `RecoveryKind` (`SkippedOperator`, `CappedStream`, `CappedDepth`, `SkippedPageTreeEntry`, `UnmappedGlyph`, `DecoderFallback`), page index, object number and a short reason. Done when: the type is used by `DisplayListBuilder` and documented in `recto_pdf.h`.
- [ ] Record a note whenever the interpreter skips a malformed operator instead of ending the stream (pdfcraft #3568, #3565). Done when: the three-operand `re` fixture renders the rest of the page and carries one `SkippedOperator` note.
- [ ] Return `Truncated` from every capped walk (form nesting, pattern recursion, stream size, glyph count) with the cap that fired. Done when: a fixture one item over each cap yields `Truncated` and one item under yields success.
- [ ] Prove that dropped content always leaves a note, by evidence of the drop rather than by what the page looks like: an instrumented test build counts every interpreter branch that skips, caps or fails on content, and `UserClasses.RecoveryContract` asserts each hit recorded a note, while valid blank pages (a zero-area fill, an empty Form XObject, paint fully outside the clip, a stream of only state changes) record none (pdfcraft #3751, #3656). Done when: every §1 fixture's branch-hit count equals its note count and the valid-blank fixtures have zero of both.
- [ ] Expose the notes in `recto-cli render --json` and `recto-cli info --json` as `recovered` arrays. Done when: the CLI JSON for the bad-`/Kids` fixture lists the skipped entry.
- [ ] Commit: `pdf: carry RecoveryNotes and Truncated outcomes so no content is dropped silently`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.RecoveryContract` passes under ASan, and `recto-cli info tests/corpus/broken/user-classes/bad-kids.pdf --json` prints a non-empty `recovered` array.

## 3. Page notice for recovered or truncated content

Users of pdfcraft could not tell a broken file from a broken viewer (pdfcraft #538, #3751, #2722). When a visible page carries `RecoveryNote` records (§2), Recto shows a quiet, dismissible notice on that page naming what was skipped, with `Copy details` for a bug report. It never blocks reading and never appears for clean pages.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: no SPEC screenshot shows it. It follows `design/SPEC.md` section 9.2 (page loading states) and section 4 tokens, using a stock WinUI `InfoBar` (`Severity=Informational`) in the page overlay slot.
**Job:** A reader opening a damaged PDF sees which page lost content and why, and can copy the details into an issue. Consumer: the reader and the `D07 T03 §2` diagnostics copy.
**Treatment:** One `InfoBar` per page that has notes, collapsed to one line, expanding to the note list. Cheaper substitute that fails the checkpoint: a log-only record or a modal error dialog.
**Chrome:** Stock `InfoBar`, `RectoColors.xaml` brushes, Segoe Fluent Icons `E946` (info); no new colours or glyph fonts.

**Build order:**

1. Add `RecoveredContentNotice` in `src/app/controls/RecoveredContentNotice.xaml/.h/.cpp`, bound to the visible page's `RecoveryNotes`. Done when: the bad-`/Kids` fixture shows one notice and a clean corpus file shows none.
2. Wire `Copy details` to the clipboard through a formatter local to the control (`D07 T03 §2` later reuses it for `Report a problem`). Done when: the clipboard text names the page, note kinds, counts and Recto version, and no document text.

- [ ] Author `src/app/controls/RecoveredContentNotice.xaml/.h/.cpp` as a stock `InfoBar` with title "Some content on this page could not be read" and a note list. Done when: the control renders in Light, Dark and HighContrast with token brushes only. Cheaper substitute: a toast that disappears before the user reads it.
- [ ] Show the notice only for pages whose `RecoveryNotes` are non-empty, keyed per page so scrolling does not re-announce it. Done when: scrolling a 200-page clean file shows zero notices.
- [ ] Add `Copy details` writing page index, note kinds, counts and the version string (no document text) to the clipboard. Done when: the copied text contains no glyph text from the page.
- [ ] Give the notice an automation name, keep it out of the tab order when collapsed, and announce it politely once per page. Done when: Narrator reads it once when the page becomes current.
- [ ] Register `view.showRecoveryDetails` in `CommandRegistry` so it is reachable from `Ctrl+K`. Done when: typing "recovered" in the command box lists it while a noted page is current.
- [ ] Commit: `viewer: show a page notice when content was recovered or truncated`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.RecoveredContentNotice` passes on the display-session host: one notice on the bad-`/Kids` fixture, none on `tests/corpus/render/hello-world.pdf`.

## 4. Single-interpreter differential suite across every content consumer

pdfcraft had at least three content parsers (renderer, editor, redactor, exporters) that disagreed on joins, `EI` detection and limits, so edits, redactions and exports acted on content the user never saw (research record, "Several content readers that disagree"; pdfcraft #3080, #3107, #3177, #3216, #3504, #3569). Recto's architecture has one interpreter; this suite proves it stays one. Every consumer (render, `PageText`, find, find-and-replace, object listing, redaction targeting and export reading) must report the same operator sequence and glyph boxes for the same page.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/testing/operator_trace.h` exposing a canonical operator trace from the shared interpreter, and make the lexer an architectural boundary: `src/pdf/recto_pdf.h` (which today includes `lexer.h`) stops exporting `lexer.h` and `parser.h`, coordinated with `D06 T02 §5`'s public API inventory, so code outside `recto-pdf` can reach content only through the interpreter API. Headers under `src/pdf/testing/` are the one exception, includable only from `tests/` and `fuzz/`. Done when: `scripts/check-lexer-boundary.py` passes on the tree and fails when a file outside `src/pdf/` includes any `src/pdf/` header other than `recto_pdf.h`, or when a file outside `tests/` and `fuzz/` includes a `src/pdf/testing/` header.
2. Add `UserClasses.Differential` comparing the trace and glyph boxes seen by each consumer on the §1 content fixtures plus split-`/Contents` variants. Done when: all consumers agree on every fixture and a planted newline join in one consumer makes the test fail.

- [ ] Generate split-stream variants in `scripts/build-corpus.py`: every operator of a 20-operator page split between each pair of tokens across two `/Contents` streams, a stream ending inside a `%` comment and inside a string (pdfcraft #3080, #3081, #3113, #3217). Done when: the variants exist with provenance rows.
- [ ] Author `src/pdf/testing/operator_trace.h/.cpp` producing a canonical trace (operator, operands, CTM, text state, marked-content stack) from the shared interpreter. Done when: two calls on the same page yield identical traces.
- [ ] Author `scripts/check-lexer-boundary.py` (run in CI) failing when any file outside `src/pdf/` includes a `src/pdf/` header other than `recto_pdf.h` (except `src/pdf/testing/` headers included from `tests/` or `fuzz/`), when `recto_pdf.h` includes `lexer.h` or `parser.h`, or when a project outside `recto-pdf.vcxproj` compiles a `src/pdf/` source; semantic agreement is the differential suite's job, not a name scan. Done when: `python -m unittest tests.scripts.test_lexer_boundary` passes and a fixture include of `pdf/lexer.h` from `src/services/` makes the script fail.
- [ ] Compare render, `PageText`, `SearchEngine`, `text_replace`, object listing and redaction target selection on every fixture. Done when: all six consumers report identical glyph boxes within 0.01 pt.
- [ ] Assert that clip-mode text (`7 Tr`) and text in OFF optional content are classified as not visible by every consumer, and that visible vertical `Identity-V` text gets identical vertical glyph boxes and order in every consumer (pdfcraft #3109, #3216, #3646). Done when: each consumer marks the same glyphs invisible and reports the same vertical layout.
- [ ] Assert identical behaviour at every nesting and size cap, so no consumer has a private limit (pdfcraft #3120, #3504, #3645). Done when: the cap fixtures yield the same `Truncated` outcome in all consumers.
- [ ] Commit: `test(quality): prove one content interpreter with a differential suite across consumers`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.Differential` passes under ASan and `python scripts/check-lexer-boundary.py` exits 0, and a planted newline join in `text_replace` makes `UserClasses.Differential` fail.

## 5. Save integrity fault-injection suite

pdfcraft replaced files before checking them, skipped fsync, ignored close errors, cleared the dirty flag on partial success, lost ACLs and creation dates, overwrote read-only files and kept deleted pages in incremental revisions (research record, save classes; pdfcraft #1698, #1804, #2165, #2366, #2658, #2921, #3409, #3496). This suite fault-injects every step of Recto's write path and asserts the original file is byte-identical on any failure, the document stays dirty, and a successful save reopens with the same page count, outlines, page labels, Info and fields. Fixes land in `D04 T02 §1` (`AtomicFileWriter`) and `D02 T01 §8` (save commands).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add a fault-injection seam to `AtomicFileWriter` (`write`, `flush`, `close`, `verify`, `replace` failure points) compiled only into `recto-tests`. Done when: each point can be failed from a test without touching production code paths.
2. Add `UserClasses.SaveIntegrity` covering each failure point, metadata preservation and the save-state machine. Done when: every case passes and removing the verify step makes the test fail.

- [ ] Fail `write`, `FlushFileBuffers`, `CloseHandle` and verification reopen in turn and assert the destination SHA-256 is unchanged and the tab stays dirty (pdfcraft #1698, #1804, #3409). Done when: all four cases pass.
- [ ] Simulate the documented `ReplaceFileW` partial-failure states, not only an early return: `ERROR_UNABLE_TO_REMOVE_REPLACED` (1175), `ERROR_UNABLE_TO_MOVE_REPLACEMENT` (1176) and `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` (1177), with `AtomicFileWriter` passing a backup file name and reconciling each state as `D04 T02 §1` defines (1175 and 1176 leave the original in place, 1177 moves it back from the backup); assert the original content is back under its own name, a failed rollback is reported with the backup's path, and no file is left without a recoverable copy. Done when: each state's fixture ends with the destination hash equal to the original or a reported backup path that holds it.
- [ ] Assert verification reopens the temp file with Recto's parser and compares page count, `/Outlines`, `/PageLabels`, `/Info`, `/AcroForm` field count and page-tree acyclicity before replacing (pdfcraft #2366, #2367, #2365). Done when: a planted writer that drops `/PageLabels` aborts the save.
- [ ] Assert replace preserves the destination ACL, attributes and creation time, and that a destination with more than one hard link gets a prompt (replace cannot keep other links pointing at the new data) (pdfcraft #2658). Done when: the test compares `GetFileSecurityW` and `GetFileInformationByHandle` before and after, and a two-link fixture shows the prompt.
- [ ] Assert a read-only destination produces a Save As prompt and an access-denied replace falls back to Save As with the cause shown (pdfcraft #2658, #2921, #2751). Done when: both cases leave the original untouched.
- [ ] Assert a destination changed on disk since open (size and last-write stamp) triggers the "changed on disk" prompt, and Recto's own save never triggers it (pdfcraft #1802, #3443). Done when: both cases pass.
- [ ] Assert any uncommitted editor buffer (text edit, form field, annotation draft) is committed or the save is refused with a reason (pdfcraft #166, #811, #3496). Done when: typing in a field and pressing `Ctrl+S` saves the typed value.
- [ ] Assert a page delete, an image delete and redaction force a full rewrite, and an unreferenced-object scan of the saved file finds none of the deleted objects (pdfcraft #2165, #3407, #3511). Done when: the scan finds zero deleted object streams.
- [ ] Assert open followed by save with no edit leaves every page content stream byte-identical (pdfcraft #3180). Done when: stream hashes match before and after.
- [ ] Assert batch and job outputs never overwrite one another on the same base name (pdfcraft #799, #1795, #1805). Done when: two same-named inputs yield two distinct outputs.
- [ ] Assert dirty state round-trips through open, edit, save, undo, redo and revert, and undo back to the saved state is clean (pdfcraft #1310, #1311, #1312). Done when: the state-machine table passes row by row.
- [ ] Commit: `test(quality): fault-inject every save step and prove originals survive`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.SaveIntegrity` passes, and deleting the verification reopen in `AtomicFileWriter` makes it fail.

## 6. Redaction completeness and audit suite on raw bytes

pdfcraft's redaction left text in `/ActualText`, `/Alt`, the structure tree, annotation replies, outlines, OFF layers, pattern cells and old revisions, blanked whole scans when one line was marked, and its audit was wrong in both directions (research record, redaction classes; pdfcraft #283, #389, #1141, #1844, #2659, #2874, #3122, #3302, #3646, #3647, #3117, #3118). This suite redacts a planted secret through every channel and then searches the saved file's raw decoded objects, not Recto's own extractor, for any trace. Fixes land in `D05 T01 §10`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Generate `tests/corpus/redaction/` fixtures carrying the string `RECTO-SECRET-4711` in each channel, and author `tests/recto-tests/raw_object_scan.cpp` decoding every stream and string object in a saved file. Done when: the scanner finds the secret in every unredacted fixture.
2. Add `UserClasses.RedactionComplete` and `UserClasses.RedactionAudit`. Done when: after apply the scanner finds zero traces, and the audit flags every planted fake cover and none of Recto's own applied redactions.

- [ ] Plant the secret in content text, `/ActualText` (marked content and struct element), `/Alt`, `/E`, `/ParentTree` elements, an annotation and its `/IRT` reply and popup, an outline title, a named destination, `/Info`, XMP, a thumbnail, an attachment name, an earlier incremental revision, an OFF optional-content group, a tiling pattern cell, a Type 3 glyph procedure and a deep form XObject. Done when: each fixture exists with a provenance row.
- [ ] Assert that apply removes every channel and the raw scan of the saved bytes finds nothing. Done when: all channel fixtures pass.
- [ ] Assert a partly covered DCT, JPX, JBIG2, CCITT, Flate and inline image keeps every pixel outside the mark unchanged and no original stream, `/SMask` or EXIF thumbnail survives (pdfcraft #283, #1771, #3124, #3359). Done when: a pixel diff outside the mark is zero and the raw scan finds no original image stream.
- [ ] Assert any cap or unreadable object met during redaction refuses the whole apply and leaves the document unchanged (pdfcraft #3645). Done when: the capped-forms fixture yields an error and the file hash is unchanged.
- [ ] Prove removal with oracles that do not depend on literal bytes: a fixture shows the secret through a custom CID encoding whose `/ToUnicode` rebuilds it, and after apply Recto's extractor, an independent extractor (Poppler `pdftotext` pinned in `tests/tools/`), the operator trace (no glyph-showing operator left inside the mark) and a render of the marked region (only the cover colour) all agree the secret is gone (review finding: literal-byte absence cannot prove removal). Done when: leaving the CID-encoded text in place makes at least two of the four oracles fail.
- [ ] Assert pattern search finds E.164 and national phone formats, IBAN (mod-97), card numbers (Luhn) and national IDs with word boundaries, so no fragment of a match survives (pdfcraft #525, #1884, #3205). Done when: a page with phone, IBAN, SIRET and NIR yields full matches only.
- [ ] Assert the audit detects covers drawn as `re f`, `re h f`, `m l l l h f`, `b`, `b*`, thick strokes, black highlights, white rectangles, opaque square annotations and pending `/Redact` marks, and reports zero findings on Recto's own applied output (pdfcraft #1817, #1885, #2385, #2786, #3117, #3118). Done when: the positive and negative control tables pass.
- [ ] Assert a pending mark renders unmistakably as pending and closing with unapplied marks asks first (pdfcraft #2864, #2934). Done when: the close path returns the unapplied-marks prompt.
- [ ] Commit: `test(quality): prove redaction removes every channel on raw bytes and audit both ways`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.RedactionComplete:UserClasses.RedactionAudit` passes, and skipping `/ActualText` removal in the redactor makes `UserClasses.RedactionComplete` fail.

## 7. Unicode authoring round-trip suite for every text-writing path

pdfcraft wrote `?` for Polish, Czech, Greek, Cyrillic, Thai, Arabic, Hebrew and CJK text in Add text, Fill and Sign, form appearances, headers, watermarks and signature appearances, reported success, and swapped the original font when one glyph was missing (research record, "Text written as `?`" and "Edit swapped the font"; pdfcraft #125, #347, #506, #1026, #1388, #2095, #2509, #3202, #3592). Every Recto path that writes text must either embed a Unicode subset with a `/ToUnicode` map that round-trips, or refuse at typing time naming the character. Fixes land in the owning writers.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `tests/recto-tests/unicode_authoring.cpp` with a script table (Latin Extended-A and B, Vietnamese, Cyrillic extended, Greek, Hebrew, Arabic, Thai, Devanagari, Bengali, CJK, Hangul) and a driver that writes each string through each path, saves, reopens and extracts. Done when: the driver runs every path on a blank fixture.
2. Add `UserClasses.UnicodeAuthoring` asserting exact extraction, no `?` and no `.notdef` in the saved file. Done when: the suite passes and a planted WinAnsi-only encoder makes it fail.

- [ ] Drive FreeText and typewriter annotations (`D03 T03 §2`), form field appearances (`D03 T03 §6`), find-and-replace (`D03 T01 §9`), headers, footers and watermarks (`D05 T01 §5`, `§6`) and visible signature appearances (`D05 T01 §8`). Done when: each path is listed in the driver table.
- [ ] Assert each saved string extracts exactly through `/ToUnicode` and renders with no `.notdef` glyph. Done when: all script rows pass for all paths.
- [ ] Assert the saved bytes contain no `?` substitution for any input character (pdfcraft #125, #506). Done when: a byte scan of the written content streams finds no unexpected `0x3F`.
- [ ] Assert editing text in an embedded subset keeps the original font for every glyph it has and substitutes only the missing glyph, naming it in the result (pdfcraft #1026, #1388, #2750). Done when: the font resource of unchanged runs is identical after the edit.
- [ ] Assert Arabic and Hebrew runs keep joining forms and logical order after save and reopen (pdfcraft #1039, #2878, #3277). Done when: extraction returns the input string in logical order.
- [ ] Assert a character no available font can draw is refused before commit with the code point named, never after save (pdfcraft #3154, #3592). Done when: the path returns `UnsupportedGlyph` with `U+XXXX` before writing.
- [ ] Commit: `test(quality): round-trip every script through every text-writing path`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.UnicodeAuthoring` passes, and forcing the header/footer writer to WinAnsi makes it fail.

## 8. UI glyph coverage, non-Latin file names and input conventions suite

pdfcraft's interface showed boxes for Chinese, Thai, Korean, Hebrew and Arabic in its own UI and in tab titles, recent files and bookmarks, because its fallback was chosen by probing one Arabic character; its scrolling ignored Windows touchpad and wheel settings (research record, "UI fonts chosen by one probe character"; pdfcraft #381, #407, #586, #651, #1904, #3082, #3234). This suite renders every localized string and a set of non-Latin file names in each shell surface and asserts real glyphs, and checks the input conventions users asked for.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Author `tests/recto-tests/ui_glyph_coverage.cpp` using DirectWrite font fallback (`IDWriteFontFallback::MapCharacters`) to resolve every code point of every string in `src/app/Strings/*/Resources.resw` and of a non-Latin file-name list. Done when: the test lists zero unmapped code points.
2. Open fixtures named in Simplified and Traditional Chinese, Japanese, Korean, Thai, Hebrew, Arabic, Devanagari and with emoji, and capture the tab, title bar, recent list and outline panel. Done when: no capture contains a `.notdef` box.

- [ ] Assert UI fallback is resolved per code point through DirectWrite, never by a single probe character, and that no XAML style hard-codes a Latin-only font family (pdfcraft #651, #3082, #3592). Done when: a source scan finds no hard-coded font file path in `src/app`.
- [ ] Assert non-Latin file names round-trip through `RecentFilesStore` as wide paths and reopen (pdfcraft #664). Done when: the recent list reopens every fixture.
- [ ] Assert outline titles decode PDFDocEncoding, UTF-16BE and UTF-16LE with BOM and UTF-8 with BOM without `U+FFFD` (pdfcraft #142, #210). Done when: the outline fixture shows the expected titles.
- [ ] Assert wheel scrolling honours the Windows "lines to scroll" setting and precision-touchpad deltas pass through without a second easing layer (pdfcraft #444, #1904, #3234). Done when: a synthetic wheel notch moves the viewport by the system line count times the line height.
- [ ] Assert `Ctrl` state is read on every wheel event so a released `Ctrl` never leaves wheel zoom stuck (pdfcraft #991). Done when: the test releases `Ctrl` between notches and sees a scroll, not a zoom.
- [ ] Assert collapsing a side panel keeps the visible page anchored (pdfcraft #1056). Done when: the top visible page index is unchanged after the collapse.
- [ ] Commit: `test(quality): prove UI glyph coverage, non-Latin names and Windows input conventions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.UiGlyphCoverage:UserClasses.InputConventions` passes on the display-session host with zero unmapped code points.

## 9. Printer driver compatibility and print failure isolation suite

pdfcraft's printing was its most reported failure: only "Save as PDF" listed, 53 s to two minutes to open the dialog with one offline WSD printer, a whitespace-padded capability value rejecting a whole driver, forced colour and duplex settings, a fixed paper list, blank pages, and driver crashes that took the app down (research record, print classes; pdfcraft #160, #2192, #2422, #2430, #2563, #2684, #2826, #2922, #3472). This suite runs Recto's print pipeline against stub and real drivers. Fixes land in `D07 T01 §2`.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add a print-target seam in `src/app/printing/` that tests can back with a stub printer (configurable delay, capability XML and failure) and with real queues (Microsoft Print to PDF, Microsoft XPS Document Writer). Done when: `PrintService` runs a job against the stub without a dialog.
2. Add `UserClasses.PrintDrivers` covering enumeration latency, tolerant capability parsing, settings, isolation and output fidelity. Done when: every case passes and a planted synchronous capability query makes the latency case fail.

- [ ] Assert the print dialog is ready in under 1 s with a stub printer that sleeps 60 s on capability queries, and that printer shows "unavailable, retry" rather than vanishing (pdfcraft #2192, #2907). Done when: the measured ready time is under 1 s.
- [ ] Add a driver capability XML corpus under `tests/corpus/print/` (padded `xsd:integer` values, unknown features, a malformed feature) and assert parsing skips the bad feature and keeps the job (pdfcraft #2430, #2830). Done when: every corpus file yields a usable capability set.
- [ ] Assert the PrintTicket sends only settings the user changed, so driver defaults for colour, duplex and paper apply (pdfcraft #2422). Done when: an untouched job's ticket contains no colour or duplex override.
- [ ] Assert the paper list comes from the driver, including custom forms and roll sizes, and the default is the document page size at 100% when the driver lacks a match (pdfcraft #1103, #2826, #3096). Done when: a stub exposing a 62 by 100 mm form lists it and prints a non-blank page of that size.
- [ ] Assert print settings persist per printer across restarts (pdfcraft #2001, #2156). Done when: a second session preselects the first session's paper, duplex and scale.
- [ ] Assert a driver failure or hang in the print worker leaves the viewer responsive and shows the error chain (pdfcraft #2684, #2873, #2922). Done when: a stub that crashes the worker yields an error dialog and the document stays open.
- [ ] Print the Type 3 fixture, an embedded TrueType subset and a CMYK PDF/X-1a file to Microsoft Print to PDF and compare against the screen render (pdfcraft #3472, #3132, #2861). Done when: the comparison passes within the golden tolerance.
- [ ] Assert spool size for an 8-page scan stays under a recorded budget and raster resolution never exceeds the printer's native dpi (pdfcraft #2058, #2514). Done when: the spool file size is under budget.
- [ ] Assert the printed page for "Current page" is the viewed physical page when page labels restart (pdfcraft #1308). Done when: the labelled fixture prints the viewed page.
- [ ] Assert a print-restricted document disables Print with a stated reason before the dialog opens (pdfcraft #2669). Done when: the command reports the reason and no dialog opens.
- [ ] Commit: `test(quality): prove print enumeration, settings, isolation and fidelity across drivers`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.PrintDrivers` passes on the display-session host, and making capability queries synchronous makes the enumeration case fail.

## 10. Render fidelity fixtures from user-reported failure classes

pdfcraft shipped a nearest-neighbour image shrink that made every scan jagged, blended against white, drew stencil masks at the wrong alpha, took an open five-point path for a rectangle (AutoCAD "M" drawn as a box), drew Type 3 glyphs bold under `2 Tr` and ignored Lab `/WhitePoint` (research record, rendering classes; pdfcraft #235, #1203, #1469, #2393, #2535, #2911, #2970, #2971, #2975). This section adds those cases to the golden harness with reference images from MuPDF and Poppler.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Generate the fixtures with `scripts/build-corpus.py` into `tests/corpus/render/user-classes/` and record reference images in `tests/goldens/user-classes/` from MuPDF and Poppler at 96 and 150 dpi. Done when: the provenance rows name the reference tool and version.
2. Add `UserClasses.RenderFidelity` comparing Recto's tiles against the references with the `D06 T01 §1` tolerance. Done when: the suite passes and switching image minification to nearest-neighbour makes it fail.

- [ ] Add a 288 dpi scan and a 1-bit CCITT line-art page shown at 61 and 96 dpi, asserting area-filtered minification regardless of `/Interpolate` (pdfcraft #2393, #2535, #3250). Done when: mean absolute error against the reference is under the recorded bound.
- [ ] Add all 16 blend modes over a transparent backdrop, knockout and isolated groups, stencil-mask alpha, pattern alpha set by a later `gs`, and fill-plus-stroke operators (pdfcraft #2970, #2971, #2975, #3176, #3229). Done when: each page matches the reference within tolerance.
- [ ] Add Word and PowerPoint style gradients under luminosity soft masks (pdfcraft #2128, #2200). Done when: no solid black polygon appears.
- [ ] Add an open five-point path that touches the corners and asserts it is stroked as a path, not filled as a rectangle (pdfcraft #235, #2911). Done when: the "M" fixture matches the reference.
- [ ] Add Type 3 text under `2 Tr`, Lab with a non-default `/WhitePoint`, `/DefaultRGB`, Indexed `hival` and 16-bit images (pdfcraft #1201, #1203, #1469). Done when: each matches the reference within tolerance.
- [ ] Add A4 at 72 dpi asserting a `596 x 842` raster (ceiling, not floor) (pdfcraft #102). Done when: the raster size matches.
- [ ] Add non-embedded CJK CID fonts (Adobe-GB1, Adobe-CNS1, Adobe-Japan1, Adobe-Korea1) and non-embedded ArialMT with Cyrillic, asserting system-font substitution draws every glyph (pdfcraft #234, #2908, #3341). Done when: no `.notdef` box appears in the render.
- [ ] Commit: `test(quality): add user-reported render fidelity classes to the golden harness`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.RenderFidelity` passes on x64, and forcing nearest-neighbour minification makes the scan case fail.

## 11. Text extraction, reading order and selection fidelity fixtures

pdfcraft dropped glyph names outside the Adobe Glyph List, lost text with no `/ToUnicode`, deleted real double letters as fake bold, read two-column pages across columns, could not select across pages and reversed Hebrew with numbers (research record, extraction and selection classes; pdfcraft #527, #1467, #1468, #1699, #2387, #2667, #3169, #3170, #3513, #3711). This section adds those classes to the `D06 T01 §3` expectation tables and to selection tests.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Generate the fixtures into `tests/corpus/text/user-classes/` with expected text recorded beside each, cross-checked once against Poppler `pdftotext`. Done when: provenance rows name the cross-check.
2. Add `UserClasses.TextFidelity` and `UserClasses.SelectionFidelity`. Done when: both pass and disabling the AGL suffix rules makes `UserClasses.TextFidelity` fail.

- [ ] Assert §2's `Truncated` outcome reaches `PageText` and search results as `SearchSummary::complete = false` (pdfcraft #971, #3119). Done when: a search over the capped-forms fixture reports `complete == false` and the page's `PageText` carries the note.
- [ ] Add glyph names `uniXXXX`, `uXXXX`, `f_i`, `hyphen.case`, `c251` and `C<number>`, a Type 0 font with no `/ToUnicode`, and `/ToUnicode /Identity-H` given as a name (pdfcraft #1699, #3169, #3170). Done when: extraction matches the expected text.
- [ ] Add condensed text at `Tz 20` and `Tz 80` with genuine double letters, and a glyph drawn twice on a clip edge (pdfcraft #1467, #2457, #3173). Done when: "Alle" and "WORLD" extract intact.
- [ ] Add a two-column article, a footnote-reference line and a float beside an abstract (pdfcraft #2387, #2586, #2929, #3682). Done when: reading order keeps each column contiguous.
- [ ] Add Hebrew and Arabic lines with numbers and punctuation (pdfcraft #2667). Done when: extraction returns logical order.
- [ ] Add vertical `Identity-V` text, tate-chu-yoko and `Tc`-spread labels (pdfcraft #3711, #3712, #3713). Done when: extraction inserts no spurious spaces and keeps column order.
- [ ] Assert drag selection crosses pages, auto-scrolls at the viewport edge and copies the whole span, and `Ctrl+A` can select the document (pdfcraft #527, #2110, #2639). Done when: a three-page drag copies all three pages' text in order.
- [ ] Assert search on `İ` and `ß` case folding never slices out of range (pdfcraft #816). Done when: the fuzzed case-fold set finds matches without a fault.
- [ ] Commit: `test(quality): add user-reported extraction and selection classes to text expectations`

**Test checkpoint:** `recto-tests.exe --gtest_filter=UserClasses.TextFidelity:UserClasses.SelectionFidelity` passes under ASan, and dropping the `Truncated` propagation in `SearchEngine` makes the capped-forms case fail.

## 12. Clean-machine release smoke and upgrade persistence proof

pdfcraft shipped releases without their OCR models or a fallback font the code required, with a default download link to an old version, with a launch that died silently on older CPUs and GPUs, and with upgrades that removed Start pins and saved signatures (research record, "Shipped without its models or fonts", "Startup dies silently", "Upgrade loses pins"; pdfcraft #103, #489, #1758, #2185, #2284, #2493, #3196, #3606). This section runs the built installer and portable zip on clean virtual machines before a release is tagged.

**Phase:** 6
**Needs:** windows-host, display-session, attended
**Surface:** no

**Build order:**

1. Author `tests/scripts/release_smoke.ps1` taking a release candidate folder and running the checks below in a clean Windows 10 and Windows 11 virtual machine, plus a machine with no DirectX 12 adapter (WARP only). Done when: the script exits 0 on the candidate and writes `build/release-smoke/<version>.json` naming the candidate commit SHA and the SHA-256 of every artifact it tested, taken from the bundle's `SHA256SUMS.txt` (`D07 T02 §7`).
2. Gate `D07 T02 §6`'s release runbook on that report before tagging, and run the script's `-PostPublish` mode after the release is published. Done when: the runbook refuses to tag when the report is missing, red, or names a different commit or artifact digest, and the post-publish mode checks the public links.

- [ ] Launch the installed and portable builds and assert a first frame, then close 100 times and assert exit code 0 and no crash dump each time (pdfcraft #1136, #2284, #2493). Done when: all launches and closes pass on both machines.
- [ ] Assert a failure before the first frame shows a dialog with the log path and a safe-mode (WARP) relaunch (pdfcraft #2284, #3286, #3688). Done when: a planted startup failure produces the dialog, not a silent exit.
- [ ] Run OCR on a French and a German fixture from the installed layout and assert the packaged models are found and accents survive (pdfcraft #103, #489, #524, #1758). Done when: the extracted text matches the expected strings exactly.
- [ ] Switch Windows display language to Simplified Chinese and Arabic and assert the UI glyph coverage check (§8) passes on the installed build (pdfcraft #381, #826). Done when: zero unmapped code points.
- [ ] Open a PDF from an Outlook-style temp attachment path and from a path with spaces and Unicode (pdfcraft #577, #1455). Done when: both open as tabs.
- [ ] Print to Microsoft Print to PDF from the installed build. Done when: the output page count matches the source.
- [ ] Install version N, pin to Start, set preferences, save a signature appearance, then upgrade to N+1 and assert the pin, preferences, signature and file association survive (pdfcraft #1058, #2185, #3606). Done when: all four are present after the upgrade.
- [ ] Diff the golden corpus rendered by version N against N+1 and fail on any unexplained change (pdfcraft #2393, #2970). Done when: the diff report lists no unexplained page.
- [ ] Add `-PostPublish` mode, run by the runbook after the GitHub release is public, asserting the README download links and the releases page resolve to this release's assets and their digests match the report (pdfcraft #320, #2034). Done when: post-publish mode passes on the published release and fails when a README link still names the previous version.
- [ ] Exercise the features users asked for on the installed build: Explorer thumbnails and preview (`D07 T01 §8`), the print verb and multi-file Combine (`D07 T01 §9`), and a saved signature appearance (`D05 T02 §10`). Done when: each produces its expected result from the installed layout.
- [ ] Commit: `test(release): smoke the installed artifacts on clean machines and prove upgrades keep user state`

**Test checkpoint:** `pwsh tests/scripts/release_smoke.ps1 -Candidate build/release/<version>` exits 0 on the clean virtual machines and writes a green `build/release-smoke/<version>.json` whose commit SHA and artifact digests match the candidate bundle, and swapping one artifact after the run makes the runbook gate refuse the tag.

## Verification

- `recto-tests.exe --gtest_filter=UserClasses.*` passes on x64 under ASan, and each gate was shown once to fail on a planted regression of its class (recorded in the section's review receipt).
- `pwsh tests/scripts/release_smoke.ps1` is green for the release candidate.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
