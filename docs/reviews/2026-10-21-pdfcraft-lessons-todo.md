## Round 1: none

- Writer: claude
- Run: 2026-10-20T22:09:40Z
- Skipped: astra (could not review: Could not read the candidate: both attempts to run `git show 9b8b408 --stat` failed before execution with `helper_unknown_error: setup refresh had errors`; no review was performed.)
- Skipped: gemini (returned no review)
- Lines: 1
- Digest: 0555d87060386853

No reviewer returned a review. This round is not an approval.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-20T22:12:40Z
- Effort: medium
- Lines: 45
- Digest: 3e3009b335c2fcee

Reviewed the supplied diff only. Repository cross-checks were unavailable because the command helper failed to start. Line references below use the supplied diff.

## consistency: needs-attention

1. **Pre-release smoke requires the release to exist already.** `todo/06-quality/TODO-03-user-failure-gates.md:349` requires the public download page and README links to return the candidate’s assets, while §12 must pass before tagging. **Reproduce:** prepare unpublished N+1 while N remains public. The required link check fails despite correct candidate artifacts. Separate pre-publication artifact verification from post-publication link verification.

2. **The overdue-fix rule blocks the release that would resolve the overdue fix.** `todo/07-release/TODO-03-listening-to-users.md:160` fails whenever a critical fix has remained unreleased for eight days, without exempting a candidate containing that fix. **Reproduce:** merge a security fix, wait eight days, then attempt its patch release. The prescribed gate rejects it. Evaluate whether the candidate leaves the fix behind.

3. **Print restriction handling contradicts itself.** `todo/07-release/TODO-01-packaging-and-print.md:281` requires a print-dialog fallback for documents forbidding printing; the newly added §2 requirement and `todo/06-quality/TODO-03-user-failure-gates.md:275` require refusal before any print dialog opens. **Reproduce:** invoke `/p` on a document whose effective permissions prohibit printing. Both requirements cannot pass. Separate password acquisition from permission refusal.

## integration: needs-attention

4. **The scrubber cannot preserve the specified operator trace while replacing text.** `todo/07-release/TODO-03-listening-to-users.md:114` requires identical traces, but `todo/06-quality/TODO-03-user-failure-gates.md:147` defines those traces to include operands. **Reproduce:** scrub `(SECRET) Tj` into a placeholder. Its operand necessarily changes, failing `Scrub.KeepsStructure`. Define a structural comparison that explicitly excludes sanitized operands. The CLI contract also disagrees between `--keep-fonts` at line 112 and `--strip-fonts` at line 118.

5. **Dependencies omit components explicitly exercised by the gates.** `todo/06-quality/TODO-03-user-failure-gates.md:45` schedules §2 in Phase 1 with only §1 as a dependency, although its checkpoint requires `SearchSummary::complete` and its contract requires `PageText`. Similarly, line 55 schedules release smoke without dependencies on the signature appearance library, UI coverage suite, or new shell handlers. **Reproduce:** execute only declared prerequisites. Search propagation cannot be tested at §2, and release smoke can complete before these shipping features exist. Add the actual prerequisites or split the gates by availability.

6. **Windows 11 integration is not delivered for the primary installer.** `todo/07-release/TODO-01-packaging-and-print.md:284` assigns modern-menu registration to “the sparse package identity of the MSIX build,” while the Inno installer receives classic verbs and MSIX remains optional. **Reproduce:** install the normal Full package without MSIX on Windows 11. No required sparse-package installation or registration step gives that installation modern-menu identity. Specify its manifest, installation, upgrade and removal lifecycle. [Microsoft’s integration requirements](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer).

## source-defect: needs-attention

7. **Valid non-painting content is classified as corruption.** `todo/06-quality/TODO-03-user-failure-gates.md:100` forbids an empty display list for every non-empty content stream. **Reproduce:** use a valid stream containing only `q Q`, comments, or graphics-state changes. It legitimately paints nothing, but the requirement forces a recovery warning, contradicting the clean-page requirement. Test omitted painting operations, not stream non-emptiness.

8. **Vertical text is incorrectly required to be invisible.** `todo/06-quality/TODO-03-user-failure-gates.md:150` groups `Identity-V` with clip-only and disabled-layer text, requiring each consumer to mark those glyphs invisible. **Reproduce:** render ordinary vertical CJK text using `Identity-V` and `0 Tr`. Correct rendering is visible. `Identity-V` selects vertical writing, not invisibility. [ISO 32000-2 CMap definitions](https://developer.adobe.com/document-services/docs/assets/5b15559b96303194340b99820d3a70fa/PDF_ISO_32000-2.pdf).

9. **The replacement fault model omits documented partial failures.** `todo/06-quality/TODO-03-user-failure-gates.md:169` treats failure of `ReplaceFileW` as a single injectable event followed by an unchanged destination. Windows documents failures where the original has moved to another name, and another where it no longer exists when no backup was supplied. **Reproduce:** simulate the filesystem states accompanying errors 1176 and 1177, rather than simply returning failure before doing anything. The plan needs backup/reconciliation and rollback-failure tests to substantiate its preservation promise. [ReplaceFileW return semantics](https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-replacefilew).

10. **A cancellation token cannot enforce the shell handler’s advertised hard limits.** `todo/07-release/TODO-01-packaging-and-print.md:254` requires a hard 2-second deadline and 256 MiB working-set cap “with `CancelToken`,” while line 252 permits reading 512 MiB into memory. **Reproduce:** supply a 300 MiB stream, or block inside `IStream::Read` without polling cancellation. Neither limit is enforced by the proposed mechanism. Also, preview handlers share a surrogate by default, so terminating that surrogate can affect other handlers. Specify enforceable allocation limits and an appropriately isolated watchdog boundary. [Microsoft’s preview-host architecture](https://learn.microsoft.com/en-us/windows/win32/shell/preview-handlers).

## record: needs-attention

11. **The evidence record overstates its documented coverage.** `docs/research/2026-10-20-pdfcraft-issue-lessons.md:3` claims all 1,710 issues were read in full, while line 9 acknowledges truncated comments. The record provides representative issue numbers but no inventory, export reference, classification counts or comment-level citations supporting the totals and attributed maintainer conclusions. **Reproduce:** attempt to reconstruct a “70+” category or verify a quoted causal attribution from the committed record alone. It cannot be done. Record the actual coverage limits and retain an auditable issue inventory with source anchors. This is an unsupported-evidence finding, not a claim that the research never happened.

## adversarial: needs-attention

12. **The scrubber can disclose private content while satisfying its tests.** `todo/07-release/TODO-03-listening-to-users.md:114` preserves content geometry and operator sequences, while its sanitization concentrates on strings, images and selected metadata. **Reproduce:** create an identity document containing a name converted to vector outlines, or a signature drawn as paths. Scrubbing text and images leaves the private information visibly intact; a raw string scan still passes. Arbitrary retained font data and custom objects create additional channels. The unconditional “shareable reproducer” promise needs a bounded supported-input policy, refusal of unsupported content, and explicit user inspection.

13. **Redaction’s raw-string oracle misses encoded surviving text.** `todo/06-quality/TODO-03-user-failure-gates.md:193` defines a scanner that decodes streams and strings and searches for `RECTO-SECRET-4711`. **Reproduce:** encode that visible text as custom CID values whose `/ToUnicode` mapping reconstructs the secret. Leave the text untouched. The decoded content operand does not contain the literal secret, so the raw scan can pass. Add independently interpreted text, rendered-image and object-reachability checks; literal-byte absence cannot prove removal.

14. **Release evidence is not bound to the candidate.** `todo/07-release/TODO-03-listening-to-users.md:162` checks the latest `main` CI run, and line 164 checks a version-named green JSON file. Neither requirement binds evidence to the candidate commit, artifact hashes, required workflow set or architecture matrix. **Reproduce:** retain a green smoke report, rebuild different binaries under the same version, and use a green run for another SHA. Every stated check can pass. Require matching candidate identity and artifact digests throughout.

15. **The regression rule measures file creation instead of regression coverage.** `todo/07-release/TODO-03-listening-to-users.md:139` rejects fixes that add no new test file. **Reproduce:** add a valid failing-before/passing-after case to an existing test file: rejected. Add an empty new test file without a regression: accepted. Check the identified regression and its execution evidence, not whether a filename was added.

16. **The lexer uniqueness test has no implementable detection contract.** `todo/06-quality/TODO-03-user-failure-gates.md:148` requires a source scan to identify any content tokenizer outside one filename, without defining recognizable entry points or enforceable dependencies. **Reproduce:** place a second parser behind differently named helpers or a generic byte loop. A naming scan can miss it while reporting exactly one lexer. Define an architectural boundary that tooling can actually enforce, then use behavioral differential tests for semantic agreement.

OVERALL: needs-attention

## Response to round 2

Every finding was checked against the commit; all 16 were accepted (11 in part) and fixed in the next commit:

1. D06 T03 §12 now gates tagging on candidate artifacts only; public link checks moved to a `-PostPublish` mode run after publication.
2. D07 T03 §5 fails an overdue fix only when its commit is not an ancestor of the candidate.
3. D07 T01 §9 refuses a print-restricted document with exit code 3 and no dialog, matching §2; a password still opens the password dialog.
4. D07 T03 §3 compares a structural trace (operator names, operand counts and types, non-string operands) and uses one flag, `--strip-fonts`.
5. D06 T03 §2 is engine-only; search and `PageText` propagation moved to §11, which now depends on D03 T01 §5; §12 depends on the shell handlers, verbs, signature library, bundle and §8.
6. D07 T01 §9 specifies the signed sparse package for the Inno Full install: manifest, register, re-register on upgrade, remove on uninstall, classic-verb fallback.
7. D06 T03 §2 flags only streams with painting operators; state-only streams stay clean.
8. D06 T03 §4 no longer treats `Identity-V` as invisible; vertical layout must agree across consumers.
9. D06 T03 §5 and the owner D04 T02 §1 now require `ReplaceFileW` with a backup name and test the 1175, 1176 and 1177 partial-failure states.
10. D07 T01 §8 parses in a job-limited `recto-shellrender.exe` worker (64 MiB input cap, 256 MiB job memory, 2 s wait, terminates only its own job).
11. The research record states its real coverage (body and comment truncation, approximate counts) and commits the 1,710-row issue inventory `docs/research/2026-10-20-pdfcraft-issues.csv`. Accepted in part: the research was done, but the record overstated "read in full".
12. D07 T03 §3 no longer promises a safe file: it reports kept channels (paths, fonts, unknown objects), renders preview pages for inspection, and offers `--blank-paths`.
13. D06 T03 §6 adds four independent oracles (Recto extractor, pinned Poppler `pdftotext`, operator trace, rendered region) and a CID-encoded secret fixture.
14. D07 T03 §5 binds CI runs and the smoke report to the candidate SHA and the bundle's `SHA256SUMS.txt`, with required workflows listed in a file.
15. D07 T03 §4 checks a `Regression-Test:` trailer naming a test that exists at the fix commit, with failing-before evidence in the pull request, not whether a file was added.
16. D06 T03 §4 replaces the name scan with an include boundary: `recto_pdf.h` stops exporting `lexer.h` and `parser.h`, and `scripts/check-lexer-boundary.py` fails on any outside include.
