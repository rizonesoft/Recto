# Audit - D00 T01 §2: PDF engine dependency policy (native recto-pdf, MuPDF retired)

- **Audit section:** `D06 T05 §2` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T01 §2`, stamped 2026-10-14 at `433c72e` (`docs/reviews/d00-t01-s2.json`, 2 rounds under the earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `bf2bd6b`: `Test-Path .gitmodules` -> `False`; `Test-Path third_party` -> `False`; `python scripts/todo-graph.py validate` -> `valid: 676 sections`. No `mupdf` reference under `src`, `tests`, `vcpkg.json` or `Directory.Build.props`. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T14:19:12Z
- Effort: medium
- Lines: 27
- Digest: aa37b71b9588c648

## adversarial: needs-attention

1. **P3** `todo/00-workspace/TODO-01-repo-and-build.md:91`: The checkpoint does not test the native-engine boundary it certifies. It checks two paths and TODO graph integrity, but neither C++20 compilation, `/W4 /WX`, nor the absence of gitlinks elsewhere. Reproduction: run the checkpoint and compare its checks with the promises at lines 85-88. A broken engine build does not affect any checkpoint input. This is a coverage defect, not evidence of a currently broken engine.

## consistency: approve

No findings within this section’s scope.

## integration: advisory

The fresh-clone build promise remains unverified in this audit. Running `build.cmd` would write build outputs, contrary to the read-only instruction. Static project inspection cannot establish that the current candidate builds successfully.

## source-defect: needs-attention

2. **P3** `todo/00-workspace/TODO-01-repo-and-build.md:85`: `git status` cannot establish “no submodule gitlinks.” An unchanged, committed submodule need not appear in status. Reproduce by running `git status --short` in a clean checkout containing a committed submodule, then comparing with `git ls-files --stage`. The latter exposes mode `160000`; the former does not. The acceptance criterion should inspect the index or candidate tree.

## design: advisory

Not applicable. This section owns no user surface.

## record: needs-attention

3. **P3** `todo/00-workspace/TODO-01-repo-and-build.md:88`: The ticked fresh-clone build claim lacks matching evidence in the referenced records. `docs/reviews/d00-t01-s2.json` records a fresh clone of the retired MuPDF configuration. The replacement’s `docs/reviews/00-workspace/D00-T01-s3.md:24` records a native-engine build, but does not identify it as a fresh-clone build. Reproduce by comparing those records with this item’s exact `Done when:`. This is an evidence gap, not proof that clean-clone builds fail.

I independently reproduced the findings file’s checkpoint results: `False`, `False`, and `valid: 676 sections`. Those results do not close the evidence gap above. No files were modified.

OVERALL: needs-attention

## Fixes after round 1

All three findings are P3 evidence gaps, not defects; the stamped checkpoint keeps its wording, and this audit supplies the stronger evidence:

- Findings 1 and 2 (gitlinks): `git ls-files --stage` holds no mode `160000` entry (0 gitlinks in the index), which checks the index rather than `git status`.
- Findings 1 and 3 (native build from a clean clone): every push to `main` builds in GitHub Actions from a fresh checkout. Run `39614129798` on `2390b2b` -> `Build x64 & ARM64 and run recto-tests: success`, `Build x64 Release with AddressSanitizer and run recto-tests: success`, so the in-tree `recto-pdf` engine builds with `/W4 /WX` for x64 and ARM64 and its tests pass, with no MuPDF.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T14:21:05Z
- Effort: medium
- Lines: 27
- Digest: fd575def3a3449f7

## adversarial: needs-attention

1. **P3** `todo/00-workspace/TODO-01-repo-and-build.md:91`: The checkpoint still omits the dependency boundary it certifies: gitlinks, C++20 compilation, and `/W4 /WX`. Reproduce by comparing the checkpoint with lines 85-88: none of its commands examines the index or compiler configuration. The additional audit evidence establishes current behaviour but leaves the repeatable checkpoint gap unresolved.

## consistency: approve

No findings within this section’s scope.

## integration: approve

No findings within this section’s scope.

## source-defect: needs-attention

2. **P3** `todo/00-workspace/TODO-01-repo-and-build.md:85`: The acceptance criterion still incorrectly uses `git status` to establish absence of submodule gitlinks. A clean committed submodule produces no short-status entry. Reproduce in such a checkout by comparing `git status --short` with `git ls-files --stage`, which exposes mode `160000`. The audit’s index inspection supplies evidence but does not correct this criterion.

## design: advisory

Not applicable: no user surface.

## record: needs-attention

3. **P3** `docs/reviews/06-quality/audit-d00-t01-s2.md:52`: The description of intervening changes as “TODO-only commits” is inaccurate. Reproduce with `git diff --stat 2390b2b 0733464` and `git diff 2390b2b 0733464 -- .gitattributes`: changes include checkout line-ending rules for solution, project, props, targets and command files. The cited run establishes an earlier checkout’s build, not an identical candidate checkout.

I independently verified the checkpoint results, zero index gitlinks, and the cited CI jobs’ success. Build logs identify `2390b2b` and show `/W4 /WX /std:c++20`. These close the missing historical build-evidence gap, but not findings 1-2 or the inaccurate candidate-equivalence wording. No files were modified.

OVERALL: needs-attention

## Fixes after round 2

- Findings 1 and 2 (P3): a dated audit note in `D00 T01 §2` (`e49114f`) names the index check for gitlinks and the CI build as the clean-clone proof; the stamped checklist keeps its wording.
- Finding 3 (P3): the cited run is on `2390b2b` exactly. Commits after it are not all TODO-only: `15a21a7` changed `.gitattributes` (checkout line endings for `.sln`, `.vcxproj`, `.props`, `.targets` and `.cmd`). Its own CI run `39615055510`: the ASan job succeeded; the x64 and ARM64 build job was still running at round 3. The local `build.cmd` at `e49114f` (which includes it) exited 0 with 0 warnings for x64 and ARM64 in Debug and Release and 98 tests passing.
