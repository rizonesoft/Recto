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

11. **The evidence record overstates its documented coverage.** The user-failure research record claims all 1,710 issues were read in full, while line 9 acknowledges truncated comments. The record provides representative issue numbers but no inventory, export reference, classification counts or comment-level citations supporting the totals and attributed maintainer conclusions. **Reproduce:** attempt to reconstruct a “70+” category or verify a quoted causal attribution from the committed record alone. It cannot be done. Record the actual coverage limits and retain an auditable issue inventory with source anchors. This is an unsupported-evidence finding, not a claim that the research never happened.

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
11. The research record states its real coverage (body and comment truncation, approximate counts) and commits the 1,710-row issue inventory a committed issue inventory. Accepted in part: the research was done, but the record overstated "read in full".
12. D07 T03 §3 no longer promises a safe file: it reports kept channels (paths, fonts, unknown objects), renders preview pages for inspection, and offers `--blank-paths`.
13. D06 T03 §6 adds four independent oracles (Recto extractor, pinned Poppler `pdftotext`, operator trace, rendered region) and a CID-encoded secret fixture.
14. D07 T03 §5 binds CI runs and the smoke report to the candidate SHA and the bundle's `SHA256SUMS.txt`, with required workflows listed in a file.
15. D07 T03 §4 checks a `Regression-Test:` trailer naming a test that exists at the fix commit, with failing-before evidence in the pull request, not whether a file was added.
16. D06 T03 §4 replaces the name scan with an include boundary: `recto_pdf.h` stops exporting `lexer.h` and `parser.h`, and `scripts/check-lexer-boundary.py` fails on any outside include.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-20T22:20:50Z
- Effort: medium
- Lines: 31
- Digest: a9e6282128fb7713

Reviewed the supplied diff and primary API documentation. Local reads failed with `helper_unknown_error`, so I could not cross-check `todo/README.md`, the foundation plan, generated plan or omitted CSV. References below use candidate-side diff lines. No files were modified.

## record: needs-attention

1. **Finding 7 remains partly unresolved: valid painting operators can produce nothing visible.** `todo/06-quality/TODO-03-user-failure-gates.md:99` still requires a recovery note for every blank render containing painting operators. **Reproduce:** render `0 0 0 0 re f`, invoke an empty Form XObject with `Do`, or paint entirely outside the clip. All are valid blank results. Require evidence of dropped content, not merely the presence of a painting operator.

2. **Finding 10 remains partly unresolved: the blocking read stays outside the killable boundary.** `todo/07-release/TODO-01-packaging-and-print.md:254` puts `IStream::Read` on a thread in the shared surrogate. Timing out the wait does not stop that thread. **Reproduce:** repeatedly supply streams whose `Read` never returns. Joining violates the deadline; abandoning them accumulates threads, buffers and retained objects. The render job cannot terminate these reads. Specify bounded lifetime and cleanup for the read operation, and test repeated timeouts and unloading. [`ISequentialStream::Read`](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-isequentialstream-read) provides no general timeout parameter.

3. **Finding 11 is only partly evidenced.** The user-failure research record acknowledges truncation but retains categorical maintainer attributions and approximate cluster totals without cluster membership or comment-level anchors. The described CSV schema supplies neither. **Reproduce:** reconstruct a “60+” category or identify the specific comment supporting a causal attribution from the committed record. A list of issues does not establish either. Retain the classification mapping and precise attribution anchors, or narrow the claims.

4. **Finding 12 remains unresolved for nonvisual disclosure.** `todo/07-release/TODO-03-listening-to-users.md:118` permits retained font programs and unknown private streams, while inspection covers rendered pages. **Reproduce:** place confidential data in an unreferenced private stream. The preview looks harmless and the kept-channel warning appears, but sharing still discloses the data. The previous request for a supported-input policy and refusal of unsupported content was replaced by a warning. Visual inspection cannot validate these retained channels.

5. **Finding 14 remains partly unresolved: workflow success does not prove the required matrix ran.** `todo/07-release/TODO-03-listening-to-users.md:164` checks successful workflow runs, but names no required job/matrix identities. **Reproduce:** skip the ARM64 or standard-user job conditionally while the remaining jobs succeed. The workflow-level requirement can pass without that coverage. Require successful conclusions for every mandatory job and matrix combination. GitHub explicitly documents that [skipped jobs can report success](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-jobs-with-conditions).

## source-defect: needs-attention

6. **The replacement prescription uses an unsupported flag and assumes a backup in every failure state.** `todo/04-convert/TODO-02-pdf-conversions-a.md:86` specifies `REPLACEFILE_WRITE_THROUGH` and reconciliation “from the backup.” Microsoft marks that flag unsupported. For 1175, both files retain their names; for 1176 with a backup name supplied, both also retain their original names. Only 1177 explicitly places the original under the backup name. **Reproduce:** inject the documented 1175 state with the original still at the destination and no backup. Mandatory restoration from the backup cannot work. Define state-specific reconciliation and remove reliance on the unsupported flag. [ReplaceFileW documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).

7. **Sparse-package registration omits COM activation registration and user scope.** `todo/07-release/TODO-01-packaging-and-print.md:284` specifies the context-menu declaration but not the corresponding `windows.comServer` registration. It also leaves the account running registration/removal undefined. **Reproduce:** implement only the specified menu declaration, or install under another administrator’s credentials and inspect the initiating user’s Explorer menu. The former lacks the specified COM server mapping; the latter can register identity for the wrong account. Require the COM declaration and an explicit per-user installation/removal lifecycle. Microsoft documents [both manifest extensions](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer), and [`Add-AppxPackage` installs to a user account](https://learn.microsoft.com/en-us/powershell/module/appx/add-appxpackage).

8. **The response misstates the memory guarantee.** `docs/reviews/2026-10-21-user-failure-gates-todo.md:79` calls this “256 MiB job memory,” but the prescribed flag at `todo/07-release/TODO-01-packaging-and-print.md:254` is `JOB_OBJECT_LIMIT_PROCESS_MEMORY`. That limits committed memory per process, not aggregate job memory or the surrogate’s copy buffer. **Reproduce:** run two processes in the job, each committing 200 MiB. Neither breaches the prescribed limit. Correct the claim or require `JOB_OBJECT_LIMIT_JOB_MEMORY` and test aggregate usage. [Job memory limit definitions](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information).

## consistency: needs-attention

9. **`--blank-paths` violates the new structural-trace invariant.** `todo/07-release/TODO-03-listening-to-users.md:114` requires every non-string operand to remain identical; line 119 explicitly changes path coordinates. **Reproduce:** scrub an outlined-name fixture with `--blank-paths` and compare structural traces. Coordinate operands necessarily differ. Define flag-specific invariants and checkpoints.

10. **The lexer boundary rejects the testing API introduced beside it.** `todo/06-quality/TODO-03-user-failure-gates.md:142` introduces `src/pdf/testing/operator_trace.h`, while line 148 forbids every external include except `recto_pdf.h`. **Reproduce:** include the trace header from `tests/recto-tests/` to implement the prescribed differential suite. The boundary script must reject it. Specify an explicit test-only interface exception or another permitted access mechanism.

11. **The atomic writer retains incompatible replacement instructions.** `todo/04-convert/TODO-02-pdf-conversions-a.md:83` requires `MoveFileExW(...MOVEFILE_REPLACE_EXISTING...)` for overwriting existing files; line 86 requires `ReplaceFileW` for those files and reserves `MoveFileExW` for new files. **Reproduce:** implement the earlier checkbox literally. Its overwrite test passes while violating the added metadata-preservation contract. Replace the earlier instruction with one coherent branching contract.

OVERALL: needs-attention

## Response to round 3

All 11 findings were checked against the commit and accepted; fixed in the next commit:

1. D06 T03 §2 now proves drops by an instrumented branch-hit count equal to the note count; valid blank pages (zero-area fill, empty form, fully clipped paint, state-only stream) record none.
2. D07 T01 §8 reads the stream synchronously on the calling thread in 1 MiB chunks with a 64 MiB cap and a 1 s total deadline, creating no thread that can outlive the call; the platform limit (no timeout inside one `IStream::Read`) is recorded for `docs/security-audit.md`, with a 200-call leak test.
3. The research record narrows its claims: "Mistake" is the cited thread's own diagnosis, and the "Reports" column is a rough rank estimate that cannot be recounted because no per-issue classification was kept.
4. D07 T03 §3 removes non-visual channels by default (unreachable objects dropped, fonts stripped, unknown streams, JavaScript, XFA and attachment data zeroed), with explicit `--keep-fonts` and `--keep-unknown` opt-ins that the report names.
5. D07 T03 §5 requires `success` for every job and matrix combination in `.github/release-required-jobs.txt` for the candidate SHA, so a skipped job fails the gate.
6. D04 T02 §1 drops the unsupported `REPLACEFILE_WRITE_THROUGH` and reconciles each state on its own terms (1175 and 1176 leave the original in place, 1177 moves it back from the backup); D06 T03 §5 tests the same.
7. D07 T01 §9 declares the `windows.comServer` surrogate server beside the context-menu extension and registers the sparse package per user (first launch, `runasoriginaluser` at install, `-AllUsers` removal at uninstall).
8. D07 T01 §8 uses `JOB_OBJECT_LIMIT_JOB_MEMORY` (aggregate) with one active process; the round 2 response's "256 MiB job memory" was wrong for the flag it named.
9. D07 T03 §3 defines the trace invariant per flag: with `--blank-paths`, only path-construction operands are excluded.
10. D06 T03 §4 lets `src/pdf/testing/` headers be included from `tests/` and `fuzz/` only, and the boundary script checks that exception.
11. D04 T02 §1's earlier checkbox now branches: `MoveFileExW` for new files only, existing files only through `ReplaceFileW`.

Round 3 was the last round the independent-review skill allows; these fixes have not had a fourth independent round.
