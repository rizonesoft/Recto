# Review: port of Office's review-shape fixes

- **Candidate:** `240376a`

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:47:06Z
- Effort: medium
- Lines: 20
- Digest: 7680f14bee207377

## adversarial: approve

## consistency: approve

## integration: needs-attention

1. **P1** `scripts/hooks/pre-commit:5`: Evidence-only commits bypass the new validation. The early exit still checks only staged `todo/` paths, so changing or deleting a receipt, findings file or the legacy digest manifest alone never reaches either new check. Reproduce in a disposable checkout: delete a stamped receipt, stage only that deletion and commit. The hook exits successfully, although `python scripts/todo-graph.py validate` refuses the resulting tree. Include `docs/reviews/` in the trigger and add a regression test for an evidence-only commit. This is a static finding; no commit was attempted.

## source-defect: approve

## design: advisory

No user-facing surface changed.

## record: advisory

Verified all 25 frozen receipt digests, ran both repository pin tests, and exercised receipt validation in memory for valid input, duplicate numbering, a trailing outage and an incorrect round count. The full regression suite and claimed before-fix failures were not reproduced. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P1): `scripts/hooks/pre-commit` now runs when a commit stages anything under `todo/` or `docs/reviews/`, so an evidence-only commit (a deleted receipt, an edited freeze list) is validated too; `graph()` reads `docs/reviews/legacy-receipts.json` up front, so a malformed freeze list refuses even when no old receipt is checked against it. `test_an_evidence_only_commit_that_breaks_the_graph_is_refused` fails with the previous hook and passes now; `python -m unittest tests.scripts.test_pre_commit_hook tests.scripts.test_todo_graph` -> `Ran 95 tests`, `OK`; `validate` -> `valid: 678 sections`.